#include "i18n.hpp"

#include "dvdCopy.hpp"

#include "gamePatches.hpp"

#include <set>

#include <cctype>

#include <algorithm>

#include <atomic>

#include <mutex>

#include <SDL.h>

#include <cstdio>

#include <cstdint>

#include <cstring>

#include <memory>

#include <new>

#include <string>

#include <vector>

#include "font.hpp"

#include "renderer.hpp"

#ifdef NXDK

#include <windows.h>

#endif

namespace {

#ifdef NXDK

struct CopySummary {

  unsigned long long bytes{ 0 };

  unsigned long long totalBytes{ 0 };

  unsigned int files{ 0 };

  unsigned int failed{ 0 };

  std::unique_ptr<unsigned char[]> buffer;

  DWORD bufferSize{ 1024 * 1024 };

  std::string error;

  bool complete{ false };

  bool cancelled{ false };

  DWORD startedAt{ GetTickCount() };

};

std::string elapsedLabel(DWORD milliseconds) {

  const unsigned long seconds = milliseconds / 1000;

  char label[40] = {};

  snprintf(label, sizeof(label), "%02lu:%02lu:%02lu", seconds / 3600,

           (seconds / 60) % 60, seconds % 60);

  return label;

}

bool allocateCopyBuffer(CopySummary& summary) {

  // Reuse one large buffer for the entire directory tree, including tiny files.

  summary.buffer.reset(new (std::nothrow) unsigned char[summary.bufferSize]);

  if (!summary.buffer) {

    summary.bufferSize = 256 * 1024;

    summary.buffer.reset(new (std::nothrow) unsigned char[summary.bufferSize]);

  }

  return summary.buffer != nullptr;

}

DvdCopyProgressCallback progressCallback = nullptr;

Font* progressFont = nullptr;

void drawCopyProgress(Menu& menu, CopySummary& summary, const std::string& note, bool confirmation) {

  if (!progressCallback || !progressFont) return;

  const double fraction = summary.complete ? 1.0 : summary.totalBytes == 0 ? 0.0 :

      std::min(0.99, static_cast<double>(summary.bytes) / summary.totalBytes);

  progressCallback(menu, menu.getRenderer(), *progressFont, fraction,

      elapsedLabel(GetTickCount() - summary.startedAt), note, confirmation);

}

void pollCopyCancel(Menu& menu, CopySummary& summary) {

  SDL_Event event;

  while (SDL_PollEvent(&event)) {

    const bool cancel = event.type == SDL_CONTROLLERBUTTONDOWN &&

        (event.cbutton.button == SDL_CONTROLLER_BUTTON_B || event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK);

    if (!cancel) continue;

    // Copying is paused here; only confirmation buttons are consumed.

    bool deciding = true;

    while (deciding) {

      drawCopyProgress(menu, summary, i18n::c("A: Cancel and clean up   B: Continue"), true);

      while (SDL_PollEvent(&event)) {

        if (event.type != SDL_CONTROLLERBUTTONDOWN) continue;

        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) { summary.cancelled = true; deciding = false; }

        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B || event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) deciding = false;

      }

      if (deciding) SDL_Delay(16);

    }

    drawCopyProgress(menu, summary, summary.cancelled ? i18n::c("Stopping copy...") : i18n::c("B: Cancel copy"), false);

    return;

  }

}

void updateProgress(Menu& menu, CopySummary& summary, bool force = false) {

  if (!summary.complete && !summary.cancelled) pollCopyCancel(menu, summary);

  if (!progressCallback || !progressFont) return;

  static DWORD lastDraw = 0;

  const DWORD now = GetTickCount();

  if (!force && summary.bytes != 0 && now - lastDraw < 250) return;

  lastDraw = now;

  drawCopyProgress(menu, summary, summary.cancelled ? i18n::c("Stopping copy...") : i18n::c("B: Cancel copy"), false);

}

std::string fatxName(const char* input) {

  std::string name(input);

  for (char& c : name) {

    unsigned char value = static_cast<unsigned char>(c);

    if (value < 32 || c == '"' || c == '*' || c == '/' || c == ':' || c == '<' ||

        c == '>' || c == '?' || c == '\\' || c == '|') {

      c = '_';

    }

  }

  if (name.size() > 42) {

    size_t dot = name.find_last_of('.');

    std::string extension = dot != std::string::npos && name.size() - dot <= 5

                                ? name.substr(dot)

                                : std::string();

    size_t stemLength = 42 - extension.size();

    name = name.substr(0, stemLength) + extension;

  }

  return name.empty() ? "_" : name;

}

uint32_t readLe32(const unsigned char* bytes) {

  return static_cast<uint32_t>(bytes[0]) |

         (static_cast<uint32_t>(bytes[1]) << 8) |

         (static_cast<uint32_t>(bytes[2]) << 16) |

         (static_cast<uint32_t>(bytes[3]) << 24);

}

std::string dvdGameFolderName() {

  FILE* xbe = fopen("D:\\default.xbe", "rb");

  if (!xbe) return "DVD_GAME";

  unsigned char header[0x11C] = {};

  if (fread(header, 1, sizeof(header), xbe) != sizeof(header) ||

      readLe32(header) != 0x48454258) {

    fclose(xbe);

    return "DVD_GAME";

  }

  const uint32_t imageBase = readLe32(header + 0x104);

  const uint32_t certificateAddress = readLe32(header + 0x118);

  if (certificateAddress < imageBase || certificateAddress - imageBase > 0x7FFFFFF0u ||

      fseek(xbe, static_cast<long>(certificateAddress - imageBase + 12), SEEK_SET) != 0) {

    fclose(xbe);

    return "DVD_GAME";

  }

  unsigned char titleBytes[80] = {};

  size_t read = fread(titleBytes, 1, sizeof(titleBytes), xbe);

  fclose(xbe);

  std::string title;

  for (size_t i = 0; i + 1 < read; i += 2) {

    if (titleBytes[i] == 0 && titleBytes[i + 1] == 0) break;

    title.push_back(titleBytes[i + 1] == 0 && titleBytes[i] >= 32

                        ? static_cast<char>(titleBytes[i])

                        : '_');

  }

  while (!title.empty() && title[title.size() - 1] == ' ') title.erase(title.size() - 1);

  if (title.empty()) return "DVD_GAME";

  return fatxName(title.c_str());

}

bool measureTree(const std::string& source, unsigned long long& totalBytes, const std::atomic<bool>* stop = nullptr, const GamePatchPlan* plan = nullptr, std::string* problem = nullptr) {

  if (stop && stop->load()) return false;

  WIN32_FIND_DATAA entry = {};

  HANDLE search = FindFirstFileA((source + "*").c_str(), &entry);

  if (search == INVALID_HANDLE_VALUE) return false;

  bool succeeded = true;

  std::set<std::string> names;

  do {

    if (!strcmp(entry.cFileName, ".") || !strcmp(entry.cFileName, "..")) continue;

    if (stop && stop->load()) { FindClose(search); return false; }

    std::string child = source + entry.cFileName;

    if (plan && !(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && plan->skipFile(child.substr(3))) continue;

    if (plan) {

      std::string error;

      if (!plan->validateName(child.substr(3), error)) {

        if (problem) *problem = error;

        FindClose(search); return false;

      }

      std::string mapped = plan->destinationName(child.substr(3));

      std::transform(mapped.begin(), mapped.end(), mapped.begin(), [](unsigned char c) { return std::tolower(c); });

      if (!names.insert(mapped).second) {

        if (problem) *problem = i18n::c("Filename patch collision: ") + child;

        FindClose(search); return false;

      }

    }

    if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {

      child += "\\";

      if (!measureTree(child, totalBytes, stop, plan, problem)) succeeded = false;

    } else {

      totalBytes += (static_cast<unsigned long long>(entry.nFileSizeHigh) << 32) |

                    entry.nFileSizeLow;

    }

  } while (FindNextFileA(search, &entry));

  if (GetLastError() != ERROR_NO_MORE_FILES) succeeded = false;

  FindClose(search);

  return succeeded;

}

bool copyTree(const std::string& source, const std::string& destination,

              CopySummary& summary, Menu& menu, const GamePatchPlan& plan) {

  WIN32_FIND_DATAA entry = {};

  HANDLE search = FindFirstFileA((source + "*").c_str(), &entry);

  if (search == INVALID_HANDLE_VALUE) return false;

  bool succeeded = true;

  do {

    if (!strcmp(entry.cFileName, ".") || !strcmp(entry.cFileName, "..")) continue;

    updateProgress(menu, summary);

    if (summary.cancelled) { FindClose(search); return false; }

    std::string sourceChild = source + entry.cFileName;

    if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && plan.skipFile(sourceChild.substr(3))) continue;

    if (!plan.validateName(sourceChild.substr(3), summary.error)) { FindClose(search); return false; }

    std::string safeName = plan.destinationName(sourceChild.substr(3));

    std::string destinationChild = destination + safeName;

    if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {

      if (!CreateDirectoryA(destinationChild.c_str(), nullptr)) {

        ++summary.failed;

        succeeded = false;

        continue;

      }

      destinationChild += "\\";

      sourceChild += "\\";

      if (!copyTree(sourceChild, destinationChild, summary, menu, plan)) succeeded = false;

    } else {

      HANDLE sourceFile = CreateFileA(sourceChild.c_str(), GENERIC_READ, FILE_SHARE_READ,

                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

      HANDLE destinationFile = INVALID_HANDLE_VALUE;

      if (sourceFile != INVALID_HANDLE_VALUE) {

        destinationFile = CreateFileA(destinationChild.c_str(), GENERIC_WRITE, 0,

                                      nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

      }

      if (sourceFile == INVALID_HANDLE_VALUE || destinationFile == INVALID_HANDLE_VALUE) {

        if (sourceFile != INVALID_HANDLE_VALUE) CloseHandle(sourceFile);

        if (destinationFile != INVALID_HANDLE_VALUE) CloseHandle(destinationFile);

        ++summary.failed;

        succeeded = false;

        continue;

      }

      unsigned long long remaining = (static_cast<unsigned long long>(entry.nFileSizeHigh) << 32) | entry.nFileSizeLow;

      bool copied = true;

      while (copied && remaining != 0 && !summary.cancelled) {

        DWORD bytesRead = 0;

        const DWORD requested = static_cast<DWORD>(std::min<unsigned long long>(remaining, summary.bufferSize));

        if (!ReadFile(sourceFile, summary.buffer.get(), requested,

                      &bytesRead, nullptr)) {

          copied = false;

          break;

        }

        if (bytesRead == 0 || bytesRead > requested) { copied = false; break; }

        DWORD offset = 0;

        while (offset < bytesRead) {

          DWORD bytesWritten = 0;

          if (!WriteFile(destinationFile, summary.buffer.get() + offset, bytesRead - offset,

                         &bytesWritten, nullptr) || bytesWritten == 0) {

            copied = false;

            break;

          }

          offset += bytesWritten;

          summary.bytes += bytesWritten;

        }

        remaining -= bytesRead;

        updateProgress(menu, summary);

      }

      if (summary.cancelled) copied = false;

      if (!CloseHandle(sourceFile)) copied = false;

      if (!CloseHandle(destinationFile)) copied = false;

      if (copied) ++summary.files;

      else { if (!summary.cancelled) ++summary.failed; succeeded = false; }

      if (summary.cancelled) { FindClose(search); return false; }

    }

  } while (FindNextFileA(search, &entry));

  if (GetLastError() != ERROR_NO_MORE_FILES) succeeded = false;

  FindClose(search);

  return succeeded;

}

bool removeTree(const std::string& path) {

  WIN32_FIND_DATAA entry = {};

  HANDLE search = FindFirstFileA((path + "\\*").c_str(), &entry);

  if (search == INVALID_HANDLE_VALUE) return false;

  bool success = true;

  do {

    if (!strcmp(entry.cFileName, ".") || !strcmp(entry.cFileName, "..")) continue;

    std::string child = path + "\\" + entry.cFileName;

    if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {

      if (!removeTree(child)) success = false;

    } else if (!DeleteFileA(child.c_str())) {

      success = false;

    }

  } while (FindNextFileA(search, &entry));

  if (GetLastError() != ERROR_NO_MORE_FILES) success = false;

  FindClose(search);

  return RemoveDirectoryA(path.c_str()) != 0 && success;

}

struct DiscAccess {

  DiscAccess() { lockDvdAccess(); }

  ~DiscAccess() { unlockDvdAccess(); }

};

struct CopyPreflight {

  std::mutex mutex;

  std::atomic<bool> stop{false};

  std::string target;

  std::string text{i18n::c("Checking game size and free space...")};

  bool ready{false};

  bool allowed{false};

};

static HANDLE preflightThread = nullptr;

static std::shared_ptr<CopyPreflight> activePreflight;

static void finishPreflight() {

  if (!preflightThread) return;

  if (activePreflight) activePreflight->stop = true;

  WaitForSingleObject(preflightThread, INFINITE);

  CloseHandle(preflightThread);

  preflightThread = nullptr;

  activePreflight.reset();

}

static DWORD WINAPI checkCopySpace(LPVOID argument) {

  auto& state = *static_cast<CopyPreflight*>(argument);

  DiscAccess access;

  unsigned long long bytes = 0;

  ULARGE_INTEGER free = {};

  const bool disc = !state.stop && GetFileAttributesA("D:\\default.xbe") != INVALID_FILE_ATTRIBUTES;

  GamePatchPlan plan;

  std::string problem;

  const bool prepared = disc && plan.prepare("D:\\", problem);

  const bool measured = prepared && measureTree("D:\\", bytes, &state.stop, &plan, &problem);

  const bool space = GetDiskFreeSpaceExA(state.target.substr(0, 3).c_str(), nullptr, nullptr, &free) != 0;

  char totals[160] = {};

  if (measured && space) {

    snprintf(totals, sizeof(totals), i18n::c("Game size: %.2f GB | Free space: %.2f GB | %s"),

        static_cast<double>(bytes) / 1000000000.0, static_cast<double>(free.QuadPart) / 1000000000.0,

        free.QuadPart >= bytes ? i18n::c("Enough space to copy.") : i18n::c("Not enough space."));

  }

  std::lock_guard<std::mutex> lock(state.mutex);

  state.text = !problem.empty() ? problem : measured && space ? totals : !disc ? i18n::c("Insert an Xbox game disc, then reopen this page.") :

      i18n::c("Could not check game size or free space. Reopen this page to retry.");

  state.allowed = measured && space && free.QuadPart >= bytes && !state.stop;

  state.ready = true;

  return 0;

}

class CopySpaceLabel : public MenuNonInteractive {

public:

  CopySpaceLabel(MenuNode* parent, std::shared_ptr<CopyPreflight> state)

      : MenuNonInteractive(parent, ""), state(state) {}

  std::string_view getLabel() const override {

    std::lock_guard<std::mutex> lock(state->mutex);

    text = state->text;

    return text;

  }

private:

  std::shared_ptr<CopyPreflight> state;

  mutable std::string text;

};

class CopyResult : public MenuNode {

public:

  CopyResult(MenuNode* parent, const std::string& message)

      : MenuNode(parent, i18n::c("DVD copy report")), message(message) {}

  bool renderCustomMenu(Font& font, Renderer& renderer) override {

    const float x = renderer.getSafeLeft() + (renderer.getSafeRight() - renderer.getSafeLeft()) * 0.20f;

    const float y = renderer.getSafeTop() + 10.0f;

    const float width = renderer.getSafeRight() - x;

    const float bottom = renderer.getSafeBottom() - 10.0f;

    SDL_FRect panel = { x, y, width, bottom - y };

    renderer.setDrawColor(2, 11, 24, 245);

    renderer.fillRectangle(panel);

    SDL_Rect border = { static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(bottom-y) };

    renderer.setDrawColor(0, 174, 235, 255);

    SDL_RenderDrawRect(renderer.getRenderer(), &border);

    float textY = y + 12.0f;

    textY += font.drawMenuText(i18n::c("DVD copy report"), {x+14, textY}, width-28, 0.88f, true).second + 12;

    font.drawMenuText(message, {x+14, textY}, width-28, 0.80f, true);

    const float footerHeight = font.measureMenuText(i18n::c("B: Return to copy page"), width-28, 0.80f).second;

    font.drawMenuText(i18n::c("B: Return to copy page"), {x+14, bottom-footerHeight-12}, width-28, 0.80f, true);

    return true;

  }

private:

  std::string message;

};

class CopyAction : public MenuItem {

public:

  CopyAction(MenuNode* parent, const std::string& label, const std::string& gamesFolder,

             std::shared_ptr<CopyPreflight> state)

      : MenuItem(parent, label), gamesFolder(gamesFolder), state(state) {}

  bool isSelectable() const override {

    std::lock_guard<std::mutex> lock(state->mutex);

    return state->ready && state->allowed;

  }

  void execute(Menu* menu) override {

    if (!isSelectable()) return;

    finishPreflight();

    struct DvdPollPause {

      DvdPollPause() { pauseDvdStatusPolling(); }

      ~DvdPollPause() { resumeDvdStatusPolling(); }

    } pausePolling;

    DiscAccess discAccess;

    ULARGE_INTEGER freeBytes = {};

    unsigned long long requiredBytes = 0;

    CopySummary summary;

    const bool gameDiscPresent =

        GetFileAttributesA("D:\\default.xbe") != INVALID_FILE_ATTRIBUTES;

    GamePatchPlan plan;

    const bool patchReady = gameDiscPresent && plan.prepare("D:\\", summary.error);

    const bool discReadable = patchReady && measureTree("D:\\", requiredBytes, nullptr, &plan, &summary.error);

    summary.totalBytes = requiredBytes;

    bool spaceKnown = GetDiskFreeSpaceExA(gamesFolder.substr(0, 3).c_str(), nullptr,

                                          nullptr, &freeBytes) != 0;

    const std::string destination = gamesFolder + "\\" + dvdGameFolderName();

    const std::string driveGamesFolder = gamesFolder + "\\";

    std::string message;

    if (!gameDiscPresent) {

      message = i18n::c("No launchable Xbox game disc detected in the DVD drive.");

    } else if (!discReadable) {

      message = summary.error.empty() ? i18n::c("Game disc detected, but its files could not be listed. Copy not started.") : summary.error;

    } else if (!spaceKnown) {

      message = i18n::c("Could not check free space on the selected drive. Copy cancelled.");

    } else if (freeBytes.QuadPart < requiredBytes) {

      message = i18n::c("Not enough free space. Copy cancelled.");

    } else if (!allocateCopyBuffer(summary)) {

      message = i18n::c("Not enough memory for the DVD copy buffer. Copy cancelled.");

    } else if ((GetFileAttributesA(driveGamesFolder.c_str()) == INVALID_FILE_ATTRIBUTES &&

                !CreateDirectoryA(driveGamesFolder.c_str(), nullptr)) ||

               GetFileAttributesA(destination.c_str()) != INVALID_FILE_ATTRIBUTES ||

               !CreateDirectoryA(destination.c_str(), nullptr)) {

      message = i18n::c("Destination already exists or could not be created. Nothing overwritten.");

    } else {

      updateProgress(*menu, summary);

      bool copied = copyTree("D:\\", destination + "\\", summary, *menu, plan);

      if (copied && summary.bytes == summary.totalBytes && !summary.cancelled) {

        drawCopyProgress(*menu, summary, i18n::c("Checking compatibility patches..."), false);

        copied = plan.apply(destination + "\\", summary.error);

      }

      summary.complete = copied && summary.bytes == summary.totalBytes && !summary.cancelled;

      copied = summary.complete;

      const DWORD elapsed = GetTickCount() - summary.startedAt;

      updateProgress(*menu, summary, true);

      bool cleaned = true;

      if (!copied) {

        drawCopyProgress(*menu, summary, i18n::c("Removing incomplete copy..."), false);

        cleaned = removeTree(destination);

      }

      if (copied) menu->refreshAfterDvdCopy();

      message = copied ? i18n::c("DVD copy complete: ") : summary.cancelled ? i18n::c("DVD copy cancelled: ") : i18n::c("DVD copy finished with errors: ");

      message += std::to_string(summary.files) + i18n::c(" files, ") +

                 std::to_string(summary.bytes / (1024 * 1024)) + i18n::c(" MB copied");

      if (summary.failed) message += ", " + std::to_string(summary.failed) + i18n::c(" failed");

      char dataTotal[100] = {};

      snprintf(dataTotal, sizeof(dataTotal), i18n::c(" | Total data copied: %.2f GB (%llu bytes)"),

               static_cast<double>(summary.bytes) / 1000000000.0, summary.bytes);

      message += dataTotal;

      message += i18n::c(" | Total elapsed time: ") + elapsedLabel(elapsed);

      if (!summary.error.empty()) message += " | " + summary.error;

      if (copied && !plan.description().empty()) message += " | " + plan.description();

      if (!copied) message += cleaned ? i18n::c(" | Incomplete copy removed.") :

          i18n::c(" | Cleanup failed. Incomplete folder remains: ") + destination;

    }

    result = std::make_shared<CopyResult>(parentNode, message);

    menu->setCurrentMenu(result.get());

  }

private:

  std::string gamesFolder;

  std::shared_ptr<CopyPreflight> state;

  std::shared_ptr<CopyResult> result;

};

class CopyTarget : public MenuNode {

public:

  CopyTarget(MenuNode* parent, const std::string& label, const std::string& target)

      : MenuNode(parent, label), target(target) {}

  void execute(Menu* menu) override {

    if (menu->getCurrentMenu() != this) {

      finishPreflight();

      state = std::make_shared<CopyPreflight>();

      state->target = target;

      childNodes.clear();

      addNode(std::make_shared<CopySpaceLabel>(this, state));

      addNode(std::make_shared<CopyAction>(this, i18n::c("Start DVD copy"), target, state));

      addNode(std::make_shared<MenuNonInteractive>(this, i18n::c("Creates a game folder in Games. Nothing overwritten.")));

      selected = 1;

      activePreflight = state;

      preflightThread = CreateThread(nullptr, 0, checkCopySpace, state.get(), 0, nullptr);

      if (!preflightThread) {

        state->text = i18n::c("Could not start size check. Reopen this page to retry.");

        state->ready = true;

      }

    }

    MenuNode::execute(menu);

  }

private:

  std::string target;

  std::shared_ptr<CopyPreflight> state;

};

#endif

}

#ifdef NXDK

void stopDvdCopyPreflight() { finishPreflight(); }

#endif

DvdCopyMenu::DvdCopyMenu(MenuNode* parent, const std::string& label)

    : MenuNode(parent, label) {}

void setDvdCopyProgressCallback(DvdCopyProgressCallback callback, Font& font) {

  progressCallback = callback;

  progressFont = &font;

}

void DvdCopyMenu::execute(Menu* menu) {

  if (!loaded) {

    loadTargets();

    loaded = true;

  }

  MenuNode::execute(menu);

}

void DvdCopyMenu::loadTargets() {

#ifdef NXDK

  const char* drives[] = { "F", "G" };

  for (const char* drive : drives) {

    std::string root = std::string(drive) + ":\\";

    if (GetFileAttributesA(root.c_str()) == INVALID_FILE_ATTRIBUTES) continue;

    std::string gamesFolder = root + "Games";

    addNode(std::make_shared<CopyTarget>(this, std::string(i18n::c("Copy to ")) + gamesFolder,

                                         gamesFolder));

  }

  if (getChildNodes()->empty()) {

    addNode(std::make_shared<MenuNonInteractive>(this, i18n::c("No writable HDD partition found.")));

  }

#endif

}

