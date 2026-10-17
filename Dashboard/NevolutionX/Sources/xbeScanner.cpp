#include "xbeScanner.hpp"
#include <chrono>
#include <cstring>
#ifdef NXDK
#include <windows.h>
#include <winnt.h>
#endif

#include "infoLog.hpp"
#include "timing.hpp"

#define XBE_TYPE_MAGIC (0x48454258)
#define SECTORSIZE 0x1000

XBEScanner* XBEScanner::singleton = nullptr;

XBEScanner* XBEScanner::getInstance() {
  if (!XBEScanner::singleton) {
    XBEScanner::singleton = new XBEScanner();
  }
  return XBEScanner::singleton;
}

#if SCANNER_THREADED
XBEScanner::XBEScanner() : running(true) {
  scannerThread = std::thread(threadMain, this);
}

XBEScanner::~XBEScanner() {
  running = false;
  queue.emplace("", nullptr);
  if (scannerThread.joinable()) {
    scannerThread.join();
  }
}
#endif

void XBEScanner::scanPath(const std::string& path, Callback&& callback) {
#if SCANNER_THREADED
  XBEScanner::getInstance()->addJob(path, callback);
#else
  auto job = QueueItem(path, callback);
  job.scan();
#endif
}

#if SCANNER_THREADED
void XBEScanner::addJob(std::string const& path, const Callback& callback) {
  std::lock_guard<std::mutex> lock(queueMutex);
  queue.emplace(path, callback);
  jobPending.notify_one();
}

void XBEScanner::threadMain(XBEScanner* scanner) {
  while (scanner->running) {
    QueueItem task;
    {
      std::unique_lock<std::mutex> lock(scanner->queueMutex);
      scanner->jobPending.wait(lock, [=] { return !scanner->queue.empty(); });
      if (!scanner->running) {
        break;
      }
      task = scanner->queue.front();
      scanner->queue.pop();
    }

    std::string const& path = task.first;
    std::vector<XBEInfo> xbes;
    auto scanStart = std::chrono::steady_clock::now();
    bool succeeded = scan(path, xbes);
    auto scanDuration = millisSince(scanStart);
    InfoLog::outputLine("Scanning %s %s in %lld ms (%d found)\n", path.c_str(),
                        succeeded ? "succeeded" : "failed", scanDuration, xbes.size());

    task.second(succeeded, xbes);
  }
}
#endif

XBEScanner::QueueItem::QueueItem(std::string p, XBEScanner::Callback c) :
    path(std::move(p)), callback(std::move(c)) {
  xbeData.resize(SECTORSIZE);
}

XBEScanner::QueueItem::~QueueItem() {
#ifdef NXDK
  if (dirHandle != INVALID_HANDLE_VALUE) {
    CloseHandle(dirHandle);
  }
#endif
}

void XBEScanner::QueueItem::scan() {
#ifdef NXDK
  // Empty optional scan locations (for example G:\ on an HDD without a G
  // partition) are normal. Treat them as empty instead of producing a boot
  // warning over the dashboard.
  DWORD attributes = GetFileAttributesA(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
        error == ERROR_INVALID_DRIVE) {
      results.clear();
      callback(true, results, 0);
      return;
    }
  }

  if (dirHandle == INVALID_HANDLE_VALUE) {
    if (path.empty() || path.back() != '\\') {
      path.append("\\");
    }
    InfoLog::outputLine(InfoLog::DEBUG, "Starting scan of %s\n", path.c_str());
    results.clear();
    scanStart = std::chrono::steady_clock::now();
    if (!openDir()) {
      callback(false, results, millisSince(scanStart));
      return;
    }
  }
  // Each app/game lives in its own folder directly under the configured root.
  // Prefer default.xbe, then accept another XBE directly inside that folder.
  do {
    if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
      continue;
    }
    const std::string gameFolder = path + findData.cFileName + "\\";
    std::string xbePath = gameFolder + "default.xbe";
    DWORD xbeAttributes = GetFileAttributesA(xbePath.c_str());
    if (xbeAttributes != INVALID_FILE_ATTRIBUTES &&
        !(xbeAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      processFile(xbePath);
      continue;
    }

    WIN32_FIND_DATAA xbeEntry = {};
    HANDLE xbeSearch = FindFirstFile((gameFolder + "*.xbe").c_str(), &xbeEntry);
    if (xbeSearch != INVALID_HANDLE_VALUE) {
      xbePath = gameFolder + xbeEntry.cFileName;
      FindClose(xbeSearch);
      processFile(xbePath);
    }
  } while (FindNextFile(dirHandle, &findData));
  CloseHandle(dirHandle);
  dirHandle = INVALID_HANDLE_VALUE;
  callback(true, results, millisSince(scanStart));
#endif
}

bool XBEScanner::QueueItem::openDir() {
#ifdef NXDK
  std::string searchmask = path + "*";
  dirHandle = FindFirstFile(searchmask.c_str(), &findData);
  return (dirHandle != INVALID_HANDLE_VALUE);
#else
  return false;
#endif
}

void XBEScanner::QueueItem::processFile(const std::string& xbePath) {
#ifdef NXDK
  // Read fixed fields directly. Never retain pointers into a resized buffer,
  // or reject an otherwise launchable XBE because its display title is missing.
  HANDLE file = CreateFileA(xbePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    InfoLog::outputLine(InfoLog::WARNING, "Cannot read game XBE %s (%lu)\n",
                        xbePath.c_str(), GetLastError());
    return;
  }
  const auto readLe32 = [](const unsigned char* bytes) -> uint32_t {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
  };
  unsigned char header[0x11C] = {};
  DWORD count = 0;
  if (!ReadFile(file, header, sizeof(header), &count, nullptr) ||
      count != sizeof(header) || readLe32(header) != XBE_TYPE_MAGIC) {
    InfoLog::outputLine(InfoLog::WARNING, "Invalid or incomplete game XBE: %s\n",
                        xbePath.c_str());
    CloseHandle(file);
    return;
  }
  std::string title;
  const uint32_t base = readLe32(header + 0x104);
  const uint32_t certificate = readLe32(header + 0x118);
  const DWORD size = GetFileSize(file, nullptr);
  if (size != INVALID_FILE_SIZE && certificate >= base) {
    const uint32_t offset = certificate - base;
    if (offset <= size && size - offset >= 92 &&
        SetFilePointer(file, offset + 12, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
      unsigned char name[80] = {};
      count = 0;
      if (ReadFile(file, name, sizeof(name), &count, nullptr) && count == sizeof(name)) {
        for (size_t i = 0; i < sizeof(name); i += 2) {
          const unsigned value = name[i] | (static_cast<unsigned>(name[i + 1]) << 8);
          if (!value) break;
          title.push_back(value >= 32 && value < 127 ? static_cast<char>(value) : '?');
        }
      }
    }
  }
  CloseHandle(file);
  while (!title.empty() && title.back() == ' ') title.pop_back();
  if (title.empty()) title = findData.cFileName;
  results.emplace_back(title, xbePath);
#endif
}