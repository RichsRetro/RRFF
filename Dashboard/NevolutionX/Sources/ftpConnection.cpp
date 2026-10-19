#include "i18n.hpp"
#include <assert.h>

#include <algorithm>

#include <cctype>

#include <condition_variable>

#include <mutex>

#include <iomanip>

#include <iostream>

#include <map>

#include <string>

#include "ftpServer.hpp"

#include "infoLog.hpp"

#ifdef NXDK

#include <hal/xbox.h>

#include <lwip/arch.h>

#include <lwip/debug.h>

#include <lwip/dhcp.h>

#include <lwip/errno.h>

#include <lwip/init.h>

#include <lwip/netdb.h>

#include <lwip/netif.h>

#include <lwip/opt.h>

#include <lwip/sockets.h>

#include <lwip/sys.h>

#include <lwip/tcpip.h>

#include <lwip/timeouts.h>

#include <netif/etharp.h>

#include <nxdk/mount.h>

#include <windows.h> // Used for file I/O

#include <xboxkrnl/xboxkrnl.h>

#else

#include <arpa/inet.h>

#include <netdb.h>

#include <netinet/in.h>

#include <stdio.h>

#include <stdlib.h>

#include <sys/socket.h>

#include <sys/types.h>

#include <unistd.h>

#include <cstring>

bool nxIsDriveMounted(char) {

  return true;

}

#endif

namespace {

struct TransferActivity {

  ftpServer& server;

  explicit TransferActivity(ftpServer& owner) : server(owner) { server.beginTransfer(); }

  ~TransferActivity() { server.endTransfer(); }

  TransferActivity(const TransferActivity&) = delete;

  TransferActivity& operator=(const TransferActivity&) = delete;

};

}

#ifdef NXDK

namespace {

// One queued disk write, plus one buffer filled by the receive thread.

// The caller must finish() before freeing buffers or closing the file.

class UploadWriter {

  HANDLE file;

  HANDLE worker = nullptr;

  std::mutex mutex;

  std::condition_variable changed;

  const char* pendingBytes = nullptr;

  size_t pendingSize = 0;

  bool pending = false;

  bool finishing = false;

  bool failed = false;

  unsigned long long committed;

  std::string failure;

  static DWORD WINAPI threadMain(void* context) {

    static_cast<UploadWriter*>(context)->run(); return 0;

  }

  void run() {

    std::unique_lock<std::mutex> lock(mutex);

    for (;;) {

      changed.wait(lock, [this]{ return pending || finishing; });

      if (!pending) return;

      const char* bytes = pendingBytes;

      const size_t size = pendingSize;

      lock.unlock();

      size_t written = 0;

      DWORD diskError = 0;

      bool ok = true;

      while (written < size) {

        DWORD actual = 0;

        const BOOL saved = WriteFile(file, bytes + written,

            static_cast<DWORD>(size - written), &actual, nullptr);

        diskError = saved ? 0 : GetLastError();

        written += actual;

        if (!saved || !actual) { ok = false; break; }

      }

      lock.lock();

      committed += written;

      pending = false;

      if (!ok) {

        failed = true;

        failure = "451 Disk write failed; error " + std::to_string(diskError) +

                  i18n::c(" after ") + std::to_string(committed) + " bytes saved.\r\n";

      }

      changed.notify_all();

      if (failed) return;

    }

  }

public:

  UploadWriter(HANDLE destination, unsigned long long position)

      : file(destination), committed(position) {

    worker = CreateThread(nullptr, 0, threadMain, this, 0, nullptr);

  }

  ~UploadWriter() { finish(); }

  bool available() const { return worker != nullptr; }

  bool submit(const char* bytes, size_t size) {

    std::unique_lock<std::mutex> lock(mutex);

    changed.wait(lock, [this]{ return !pending || failed; });

    if (failed || finishing) return false;

    pendingBytes = bytes; pendingSize = size; pending = true;

    changed.notify_all(); return true;

  }

  void finish() {

    if (!worker) return;

    { std::lock_guard<std::mutex> lock(mutex); finishing = true; changed.notify_all(); }

    WaitForSingleObject(worker, INFINITE);

    CloseHandle(worker); worker = nullptr;

  }

  bool getFailure(std::string& text) {

    std::lock_guard<std::mutex> lock(mutex);

    if (failed) text = failure;

    return failed;

  }

  unsigned long long bytesSaved() {

    std::lock_guard<std::mutex> lock(mutex); return committed;

  }

};

}

#endif

const std::string drives = "CDEFGXYZ";

const std::string types[] = { "ASCII", "EBCDIC", "IMAGE", "LOCAL" };

const std::string replies[] = {

  "220 Please enter your login name now.\r\n",

  "331 Password required.\r\n",

  "230 User logged in, proceed.\r\n",

  "257 \"%s\" is current directory\r\n",

  "502 %s not implemented.\r\n",

  "200 Type set to %s\r\n",

  "250 \"%s\" is current directory.\r\n",

  "215 UNIX type: L8\r\n",

  "200 Port command ok.\r\n",

  "150 Opening ASCII data connection for ls\r\n",

  "226 Data transfer finished successfully. Data connection closed.\r\n",

  "504 Command parameter not implemented.\r\n",

  "250 Requested file action ok.\r\n",

  "553 Requested action not taken.\r\n",

  "530 Not logged in.\r\n"

};

void ftpConnection::sendStdString(std::string const& s, int flags = 0) {

  sendStdString(_fd, s, flags);

}

void ftpConnection::sendStdString(int fd, std::string const& s, int flags = 0) {

  sendAll(fd, s.c_str(), s.length(), flags);

}

void ftpConnection::setTransferTimeouts(int fd) {

  timeval timeout = {};

  timeout.tv_sec = fd == _fd ? 30 : 2;

  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  timeout.tv_sec = 30;

  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

}

bool ftpConnection::sendAll(int fd, const char* bytes, size_t length, int flags) {

  size_t offset = 0;

  while (offset < length && !server->isStopping()) {

    const int count = send(fd, bytes + offset, length - offset, flags);

    if (count <= 0) {

      InfoLog::outputLine(InfoLog::ERROR, "FTP send stalled or disconnected (%d)\n", errno);

      return false;

    }

    offset += static_cast<size_t>(count);

  }

  return offset == length;

}

ftpConnection::ftpConnection(int fd, ftpServer* s) : _fd(fd), server(s) {

  // A command socket is only read when select says it is ready. Bound writes too.

  setTransferTimeouts(fd);

  // Send short command replies promptly rather than waiting for a previous

  // reply to be acknowledged (especially LIST/STOR completion replies).

  int noDelay = 1;

  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));

  buf = (char*)malloc(FTP_CMD_BUFFER_SIZE);

  pwd = "/";

  logged_in = false;

  sendStdString(replies[0]);

  mode = 'I';

}

ftpConnection::~ftpConnection() {

  closeDataConnection();

  free(buf);

  free(uploadBuffer);

  server = nullptr;

}

bool ftpConnection::update() {

  // Might as well kill the connection if buffer memory allocation failed.

  if (buf == nullptr) {

    return false;

  }

  // handle data from a client

  int nbytes;

  if ((nbytes = recv(_fd, buf, FTP_CMD_BUFFER_SIZE - 1, 0)) <= 0) {

    // got error or connection closed by client

    if (nbytes == 0) {

      // connection closed

    } else {

      InfoLog::outputLine(InfoLog::ERROR, "Error: recv\n");

    }

    /* Report back to server that we're a dud! */

    return false;

  }

  pendingInput.append(buf, static_cast<size_t>(nbytes));

  if (pendingInput.size() > 4096) return false;

  while (true) {

    size_t lineEnd = pendingInput.find_first_of("\r\n");

    if (lineEnd == std::string::npos) break;

    std::string recvdata = pendingInput.substr(0, lineEnd);

    size_t consumed = lineEnd + 1;

    while (consumed < pendingInput.size() &&

           (pendingInput[consumed] == '\r' || pendingInput[consumed] == '\n')) {

      ++consumed;

    }

    pendingInput.erase(0, consumed);

    if (recvdata.empty()) continue;

    size_t cmdDataSep = recvdata.find(' ');

    std::string cmd = recvdata.substr(0, cmdDataSep);

    std::transform(cmd.begin(), cmd.end(), cmd.begin(),

                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    std::string arg = cmdDataSep == std::string::npos

                          ? std::string()

                          : recvdata.substr(cmdDataSep + 1);

    if (!cmd.compare("USER")) {

      cmdUser(arg);

    } else if (!cmd.compare("PASS")) {

      cmdPass(arg);

    } else if (!cmd.compare("AUTH")) {

      cmdUnimplemented(cmd);

    } else if (logged_in) {

      if (!cmd.compare("NOOP")) {

        sendStdString("200 OK.\r\n");

      } else if (!cmd.compare("ABOR")) {

        closeDataConnection();

        sendStdString("225 Data connection closed.\r\n");

      } else if (!cmd.compare("CWD")) {

        cmdCwd(arg);

      } else if (!cmd.compare("CDUP")) {

        cmdCdup();

      } else if (!cmd.compare("DELE")) {

        cmdDele(arg);

      } else if (!cmd.compare("LIST")) {

        cmdList(arg);

      } else if (!cmd.compare("MDTM")) {

        // cmdMdtm(arg);

        cmdUnimplemented(cmd);

      } else if (!cmd.compare("MKD")) {

        cmdMkd(arg);

      } else if (!cmd.compare("NLST")) {

        cmdNlst(arg);

      } else if (!cmd.compare("PASV")) {

        cmdPasv();

      } else if (!cmd.compare("EPSV")) {

        cmdEpsv();

      } else if (!cmd.compare("PORT")) {

        cmdPort(arg);

      } else if (!cmd.compare("EPRT")) {

        cmdEprt(arg);

      } else if (!cmd.compare("PWD")) {

        cmdPwd();

      } else if (!cmd.compare("QUIT")) {

        sendStdString("221 Goodbye.\r\n");

        return false;

      } else if (!cmd.compare("RETR")) {

        cmdRetr(arg);

      } else if (!cmd.compare("RMD")) {

        cmdRmd(arg);

      } else if (!cmd.compare("RNFR")) {

        cmdRnfr(arg);

      } else if (!cmd.compare("RNTO")) {

        cmdRnto(arg);

      } else if (!cmd.compare("SITE")) {

        // cmdSite(arg);

        cmdUnimplemented(cmd);

      } else if (!cmd.compare("SIZE")) {

        // cmdSize(arg);

        cmdUnimplemented(cmd);

      } else if (!cmd.compare("REST")) {

        // FATX files are smaller than 4 GiB. Reject overflow and malformed offsets.

        unsigned long long offset = 0;

        bool valid = !arg.empty();

        for (unsigned char c : arg) {

          if (c < '0' || c > '9') { valid = false; break; }

          offset = offset * 10 + (c - '0');

          if (offset > 0xFFFFFFFEULL) { valid = false; break; }

        }

        restartOffset = valid ? static_cast<unsigned long>(offset) : 0;

        sendStdString(valid ? "350 Restart position accepted.\r\n" : "501 Invalid restart offset.\r\n");

      } else if (!cmd.compare("APPE")) {

        appendUpload = true;

        cmdStor(arg);

        appendUpload = false;

      } else if (!cmd.compare("STOR")) {

        cmdStor(arg);

      } else if (!cmd.compare("SYST")) {

        cmdSyst();

      } else if (!cmd.compare("TYPE")) {

        cmdType(arg);

      } else if (!cmd.compare("execute")) {

        cmdExecute(arg);

      } else {

        InfoLog::outputLine(InfoLog::DEBUG,

                            ("Received cmd " + cmd + ", arg " + arg + "\n").c_str());

        cmdUnimplemented(cmd);

      }

    } else {

      sendStdString(replies[14]);

    }

  }

  /* Tell the server that we're still alive and kicking! */

  return true;

}

void ftpConnection::cmdUser(std::string const& arg) {

  if (!arg.compare(server->conf->getUser())) {

    sendStdString(replies[1]);

  } else {

    sendStdString("530 login authentication failed.\r\n");

  }

}

void ftpConnection::cmdPass(std::string const& arg) {

  if (!arg.compare(server->conf->getPassword())) {

    sendStdString(replies[2]);

    logged_in = true;

  } else {

    sendStdString("530 login authentication failed.\r\n");

  }

}

void ftpConnection::cmdPwd() {

  sprintf(buf, replies[3].c_str(), pwd.c_str());

  sendStdString(buf);

}

void ftpConnection::cmdType(std::string const& arg) {

  if (arg[0] == 'I') {

    sprintf(buf, replies[5].c_str(), "IMAGE");

    sendStdString(buf);

    mode = 'I';

  } else if (arg[0] == 'A') {

    sprintf(buf, replies[5].c_str(), "ASCII");

    sendStdString(buf);

    mode = 'A';

  } else {

    sendStdString(replies[11]);

  }

}

void ftpConnection::cmdCwd(std::string const& arg) {

  if (arg.empty()) {

    sendStdString("501 Directory path required.\r\n");

    return;

  }

  std::string tmpPwd = "";

  if (arg == "..") {

    tmpPwd = pwd.substr(0, pwd.rfind('/', pwd.rfind('/') - 1) + 1);

  } else if (arg[0] == '/') {

    if (arg.length() > 1) {

      tmpPwd = arg + "/";

    } else {

      tmpPwd = "/";

    }

  } else {

    tmpPwd = pwd + arg + "/";

  }

#ifdef NXDK

  if (tmpPwd.length() > 1) {

    std::string tmpDosPwd = unixToDosPath(tmpPwd);

    DWORD attributes = GetFileAttributesA(tmpDosPwd.c_str());

    if (attributes != INVALID_FILE_ATTRIBUTES &&

        (attributes & FILE_ATTRIBUTE_DIRECTORY)) {

      pwd = tmpPwd;

      sprintf(buf, replies[6].c_str(), pwd.c_str());

      sendStdString(buf);

    } else {

      sendStdString("550 Directory unavailable.\r\n");

    }

  } else {

    pwd = "/";

    sprintf(buf, replies[6].c_str(), pwd.c_str());

    sendStdString(buf);

  }

#else

  pwd = tmpPwd;

  sprintf(buf, replies[6].c_str(), pwd.c_str());

  sendStdString(buf);

#endif

}

void ftpConnection::cmdDele(std::string const& arg) {

  std::string fileName = arg;

  if (arg[0] != '/') {

    fileName = pwd + arg;

  }

#ifdef NXDK

  fileName = unixToDosPath(fileName);

  if (DeleteFile(fileName.c_str())) {

    sendStdString(replies[12]);

  } else {

    sendStdString(replies[13]);

  }

#else

  cmdUnimplemented("DELE");

#endif

}

void ftpConnection::cmdCdup() {

  cmdCwd("..");

}

void ftpConnection::cmdSyst() {

  sendStdString(replies[7]);

}

void ftpConnection::cmdPort(std::string const& arg) {

  unsigned int a = 0, b = 0, c = 0, d = 0, p1 = 0, p2 = 0;

  if (sscanf(arg.c_str(), "%u,%u,%u,%u,%u,%u", &a, &b, &c, &d, &p1, &p2) != 6 ||

      a > 255 || b > 255 || c > 255 || d > 255 || p1 > 255 || p2 > 255) {

    sendStdString("501 Invalid PORT address.\r\n");

    return;

  }

  closeDataConnection();

  std::string address = std::to_string(a) + "." + std::to_string(b) + "." +

                        std::to_string(c) + "." + std::to_string(d);

  std::string port = std::to_string(p1 * 256 + p2);

  InfoLog::outputLine(InfoLog::INFO,

                      (address + " " + port + " " + std::to_string(_fd) + "\n").c_str());

  dataFd = server->openConnection(address, port);

  if (dataFd != -1) {

    sendStdString(replies[8]);

  } else {

    sendStdString("425 Socket creation failed.");

  }

}

void ftpConnection::cmdPasv() {

#ifdef NXDK

  closeDataConnection();

  sockaddr_in controlAddress = {};

  socklen_t addressLength = sizeof(controlAddress);

  if (getsockname(_fd, reinterpret_cast<sockaddr*>(&controlAddress), &addressLength) < 0) {

    sendStdString("425 Could not determine the console address.\r\n");

    return;

  }

  passiveListenFd = socket(AF_INET, SOCK_STREAM, 0);

  if (passiveListenFd < 0) {

    sendStdString("425 Could not open a passive data port.\r\n");

    return;

  }

  int yes = 1;

  setsockopt(passiveListenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in passiveAddress = {};

  passiveAddress.sin_family = AF_INET;

  passiveAddress.sin_addr = controlAddress.sin_addr;

  passiveAddress.sin_port = 0;

  if (bind(passiveListenFd, reinterpret_cast<sockaddr*>(&passiveAddress),

           sizeof(passiveAddress)) < 0 || listen(passiveListenFd, 1) < 0) {

    closeDataConnection();

    sendStdString("425 Could not listen on a passive data port.\r\n");

    return;

  }

  socklen_t passiveLength = sizeof(passiveAddress);

  if (getsockname(passiveListenFd, reinterpret_cast<sockaddr*>(&passiveAddress),

                  &passiveLength) < 0) {

    closeDataConnection();

    sendStdString("425 Could not read the passive data port.\r\n");

    return;

  }

  uint32_t address = ntohl(controlAddress.sin_addr.s_addr);

  unsigned int port = ntohs(passiveAddress.sin_port);

  std::string response = "227 Entering Passive Mode (" +

      std::to_string((address >> 24) & 255) + "," +

      std::to_string((address >> 16) & 255) + "," +

      std::to_string((address >> 8) & 255) + "," +

      std::to_string(address & 255) + "," + std::to_string(port / 256) + "," +

      std::to_string(port % 256) + ").\r\n";

  sendStdString(response);

#else

  sendStdString("502 Passive FTP is only available on Xbox.\r\n");

#endif

}

void ftpConnection::cmdEpsv() {

#ifdef NXDK

  closeDataConnection();

  sockaddr_in controlAddress = {};

  socklen_t addressLength = sizeof(controlAddress);

  if (getsockname(_fd, reinterpret_cast<sockaddr*>(&controlAddress), &addressLength) < 0) {

    sendStdString("425 Could not determine the console address.\r\n");

    return;

  }

  passiveListenFd = socket(AF_INET, SOCK_STREAM, 0);

  if (passiveListenFd < 0) {

    sendStdString("425 Could not open a passive data port.\r\n");

    return;

  }

  int yes = 1;

  setsockopt(passiveListenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in passiveAddress = {};

  passiveAddress.sin_family = AF_INET;

  passiveAddress.sin_addr = controlAddress.sin_addr;

  passiveAddress.sin_port = 0;

  if (bind(passiveListenFd, reinterpret_cast<sockaddr*>(&passiveAddress),

           sizeof(passiveAddress)) < 0 || listen(passiveListenFd, 1) < 0) {

    closeDataConnection();

    sendStdString("425 Could not listen on a passive data port.\r\n");

    return;

  }

  socklen_t passiveLength = sizeof(passiveAddress);

  if (getsockname(passiveListenFd, reinterpret_cast<sockaddr*>(&passiveAddress),

                  &passiveLength) < 0) {

    closeDataConnection();

    sendStdString("425 Could not read the passive data port.\r\n");

    return;

  }

  sendStdString("229 Entering Extended Passive Mode (|||" +

                std::to_string(ntohs(passiveAddress.sin_port)) + "|).\r\n");

#else

  sendStdString("502 Passive FTP is only available on Xbox.\r\n");

#endif

}

bool ftpConnection::prepareDataConnection() {

  if (dataFd >= 0) { setTransferTimeouts(dataFd); return true; }

  if (passiveListenFd < 0) return false;

  // Do not let an abandoned PASV listener block every other FTP client forever.

  bool ready = false;

  for (int seconds = 0; seconds < 30 && !server->isStopping(); ++seconds) {

    fd_set readable;

    FD_ZERO(&readable);

    FD_SET(passiveListenFd, &readable);

    timeval wait = {};

    wait.tv_sec = 1;

    const int result = select(passiveListenFd + 1, &readable, nullptr, nullptr, &wait);

    if (result > 0) { ready = true; break; }

    if (result < 0) break;

  }

  if (!ready) { closeDataConnection(); return false; }

  sockaddr_storage peer = {};

  socklen_t peerLength = sizeof(peer);

  dataFd = accept(passiveListenFd, reinterpret_cast<sockaddr*>(&peer), &peerLength);

  if (dataFd < 0) {

    InfoLog::outputLine(InfoLog::ERROR, "FTP passive accept failed: %d\n", errno);

  }

  close(passiveListenFd);

  passiveListenFd = -1;

  if (dataFd >= 0) setTransferTimeouts(dataFd);

  return dataFd >= 0;

}

void ftpConnection::closeDataConnection() {

  if (dataFd >= 0) {

    close(dataFd);

    dataFd = -1;

  }

  if (passiveListenFd >= 0) {

    close(passiveListenFd);

    passiveListenFd = -1;

  }

}

void ftpConnection::cmdMkd(std::string const& arg) {

#ifdef NXDK

  std::string fileName = arg;

  if (arg[0] != '/') {

    fileName = pwd + arg;

  }

  fileName = unixToDosPath(fileName);

  if (CreateDirectoryA(fileName.c_str(), NULL)) {

    sendStdString(replies[12]);

  } else {

    sendStdString(replies[13]);

  }

#else

  cmdUnimplemented("MKD");

#endif

}

void ftpConnection::cmdRmd(std::string const& arg) {

#ifdef NXDK

  std::string fileName = arg;

  if (arg[0] != '/') {

    fileName = pwd + arg;

  }

  fileName = unixToDosPath(fileName);

  if (RemoveDirectoryA(fileName.c_str())) {

    sendStdString(replies[12]);

  } else {

    sendStdString(replies[13]);

  }

#else

  cmdUnimplemented("RMD");

#endif

}

void ftpConnection::cmdRnfr(std::string const& arg) {

#ifdef NXDK

  if (arg[0] != '/') {

    rnfr = pwd + arg;

  } else {

    rnfr = arg;

  }

  sendStdString("350 File action pending further information.\r\n");

#else

  cmdUnimplemented("RNFR");

#endif

}

void ftpConnection::cmdRnto(std::string const& arg) {

  std::string fileName = arg;

  if (arg[0] != '/') {

    fileName = pwd + arg;

  }

#ifdef NXDK

  if (MoveFileA(unixToDosPath(rnfr).c_str(), unixToDosPath(fileName).c_str())) {

    sendStdString(replies[12]);

  } else {

    sendStdString(replies[13]);

  }

  rnfr = "";

#else

  cmdUnimplemented("RNTO");

#endif

}

void ftpConnection::cmdList(std::string const&) {

  if (prepareDataConnection()) {

    sendStdString(replies[9]);

    std::string listingPath = pwd;

    sendFolderContents(dataFd, listingPath);

    closeDataConnection();

    sendStdString(replies[10]);

  } else {

    sendStdString("425 Data connection unavailable; reconnect and retry.\r\n");

  }

}

void ftpConnection::cmdNlst(std::string const&) {

  if (!prepareDataConnection()) {

    sendStdString("425 Data connection unavailable; reconnect and retry.\r\n");

    return;

  }

  sendStdString(replies[9]);

  std::string listingPath = pwd;

  sendFolderContents(dataFd, listingPath);

  closeDataConnection();

  sendStdString(replies[10]);

}

void ftpConnection::cmdEprt(std::string const& arg) {

  if (arg.size() < 7) {

    sendStdString("501 Invalid EPRT address.\r\n");

    return;

  }

  char delimiter = arg[0];

  size_t familyEnd = arg.find(delimiter, 1);

  size_t addressEnd = familyEnd == std::string::npos ? familyEnd : arg.find(delimiter, familyEnd + 1);

  size_t portEnd = addressEnd == std::string::npos ? addressEnd : arg.find(delimiter, addressEnd + 1);

  if (familyEnd == std::string::npos || addressEnd == std::string::npos ||

      portEnd == std::string::npos || (arg[1] != '1' && arg[1] != '2')) {

    sendStdString("501 Invalid EPRT address.\r\n");

    return;

  }

  std::string address = arg.substr(familyEnd + 1, addressEnd - familyEnd - 1);

  std::string port = arg.substr(addressEnd + 1, portEnd - addressEnd - 1);

  if (address.empty() || port.empty()) {

    sendStdString("501 Invalid EPRT address.\r\n");

    return;

  }

  closeDataConnection();

  dataFd = server->openConnection(address, port);

  if (dataFd != -1) {

    sendStdString(replies[8]);

  } else {

    sendStdString("425 Socket creation failed.");

  }

}

void ftpConnection::cmdRetr(std::string const& arg) {

  if (prepareDataConnection()) {

    std::string fileName = arg;

    if (arg[0] != '/') {

      fileName = pwd + arg;

    }

    InfoLog::outputLine(InfoLog::DEBUG, "Trying to send file %s!\n", fileName.c_str());

    sendStdString("150 Sending file " + arg + "\r\n");

    bool transferred = sendFile(fileName);

    closeDataConnection();

    sendStdString(transferred ? replies[10] : "426 Transfer stopped: file read or connection failed.\r\n");

  } else {

    sendStdString("425 Data connection unavailable.\r\n");

  }

}

void ftpConnection::cmdStor(std::string const& arg) {

  if (prepareDataConnection()) {

    std::string fileName = arg;

    if (arg[0] != '/') {

      fileName = pwd + arg;

    }

    // Errors are logged by recvFile; avoid per-file diagnostic allocations.

    sendStdString("150 Receiving file " + arg + "\r\n");

    bool transferred = recvFile(fileName);

    closeDataConnection();

    sendStdString(transferred ? replies[10] : transferFailure);

  } else {

    sendStdString("425 Data connection unavailable.\r\n");

  }

}

void ftpConnection::cmdExecute(std::string const& path) {

#ifdef NXDK

  XLaunchXBE(const_cast<char*>(path.c_str()));

#else

  InfoLog::outputLine(InfoLog::DEBUG, "Launching %s\n", path.c_str());

#endif

}

void ftpConnection::cmdUnimplemented(std::string const& arg) {

  sprintf(buf, replies[4].c_str(), arg.c_str());

  sendStdString(buf);

}

std::string ftpConnection::unixToDosPath(std::string const& path) {

  std::string ret;

  if (path[0] == '/' && path[1] == '/') {

    ret = path.substr(1, std::string::npos);

  } else {

    ret = path;

  }

  ret = ret.substr(1, 1) + ":" + path.substr(2, std::string::npos);

  std::replace(ret.begin(), ret.end(), '/', '\\');

  if (ret[1] == 'D') {

    ret.replace(1, 2, "Device\\CdRom0");

  }

  return ret;

}

void ftpConnection::sendFolderContents(int fd, std::string& path) {

  if (path[0] == '/' && path[1] == '/') {

    path = path.substr(1, std::string::npos);

  }

  if (!path.compare("/")) {

    for (size_t i = 0; i < drives.length(); ++i) {

      if (nxIsDriveMounted(drives[i])) {

        sendStdString(fd, "drwxr-xr-x 1 XBOX XBOX 0 Jan 01 2000 " + drives.substr(i, 1)

                              + "\r\n");

      }

    }

    return;

  }

#ifdef NXDK

  WIN32_FIND_DATAA fData;

  std::string searchmask = unixToDosPath(path + "*");

  HANDLE fHandle = FindFirstFileA(searchmask.c_str(), &fData);

  if (fHandle == INVALID_HANDLE_VALUE) {

    return;

  }

  do {

    std::string retstr = "";

    if (fData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {

      retstr = "d";

    } else {

      retstr = "-";

    }

    TIME_FIELDS fTime;

    RtlTimeToTimeFields((PLARGE_INTEGER)&fData.ftLastWriteTime, &fTime);

    // Traditional Unix LIST dates keep clients from treating the time as

    // part of the filename. Newly formatted FATX entries may have zero dates.

    static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",

                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    const bool validDate = fTime.Year >= 1980 && fTime.Year <= 9999 &&

                           fTime.Month >= 1 && fTime.Month <= 12 &&

                           fTime.Day >= 1 && fTime.Day <= 31;

    char date[32] = {};

    snprintf(date, sizeof(date), "%s %02u %04u",

             validDate ? months[fTime.Month - 1] : "Jan",

             validDate ? static_cast<unsigned>(fTime.Day) : 1u,

             validDate ? static_cast<unsigned>(fTime.Year) : 2000u);

    retstr += "rwxr-xr-x 1 XBOX XBOX " + std::to_string(fData.nFileSizeLow) + " "

              + date + " " + fData.cFileName + "\r\n";

    sendStdString(fd, retstr);

  } while (FindNextFile(fHandle, &fData) != 0);

  FindClose(fHandle);

#else

  for (int q = 0; q < 10; ++q) {

    std::string retstr = "drwxr-xr-x 1 XBOX XBOX " + std::to_string(q) + " May 11 10:41 "

                         + std::to_string(q) + "\r\n";

    sendStdString(fd, retstr);

  }

  std::string retstr = "-rwxr-xr-x 1 XBOX XBOX 1024 May 11 10:41 X\r\n";

  sendStdString(fd, retstr);

#endif

}

bool ftpConnection::sendFile(std::string const& fileName) {

  TransferActivity activity(*server);

#ifdef NXDK

  const std::string filePath = unixToDosPath(fileName);

  HANDLE file = CreateFile(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,

                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (file == INVALID_HANDLE_VALUE) return false;

  const unsigned long offset = restartOffset;

  restartOffset = 0;

  LARGE_INTEGER position;

  position.QuadPart = offset;

  LARGE_INTEGER size;

  if (!GetFileSizeEx(file, &size) || position.QuadPart > size.QuadPart ||

      !SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {

    CloseHandle(file);

    return false;

  }

  char* buffer = static_cast<char*>(malloc(FTP_DATA_BUFFER_SIZE));

  if (!buffer) { CloseHandle(file); return false; }

  bool success = true;

  while (!server->isStopping()) {

    DWORD count = 0;

    if (!ReadFile(file, buffer, FTP_DATA_BUFFER_SIZE, &count, nullptr)) {

      success = false;

      break;

    }

    if (!count) break;

    if (mode == 'A') {

      for (DWORD i = 0; i < count; ++i) buffer[i] &= 0x7F;

    }

    // send may accept fewer bytes than requested; never silently discard the rest.

    if (!sendAll(dataFd, buffer, count)) { success = false; break; }

  }

  success = success && !server->isStopping();

  free(buffer);

  CloseHandle(file);

  return success;

#else

  char trash[1024] = {};

  return sendAll(dataFd, trash, sizeof(trash));

#endif

}

bool ftpConnection::recvFile(std::string const& fileName) {

  TransferActivity activity(*server);

  const std::string filePath = unixToDosPath(fileName);

  unsigned long offset = restartOffset;

  restartOffset = 0; // REST applies to this transfer only, including failures.

  transferFailure = "426 Upload interrupted.\r\n";

  unsigned long long committed = offset;

#ifdef NXDK

  HANDLE file = CreateFile(filePath.c_str(), GENERIC_WRITE, 0, nullptr,

                           appendUpload ? OPEN_ALWAYS : (offset ? OPEN_EXISTING : CREATE_ALWAYS),

                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

  if (file == INVALID_HANDLE_VALUE) {

    transferFailure = "550 Cannot open destination; disk error " + std::to_string(GetLastError()) + ".\r\n";

    return false;

  }

  if (appendUpload && !offset) {

    LARGE_INTEGER size;

    if (!GetFileSizeEx(file, &size) || size.QuadPart > 0xFFFFFFFEULL) {

      transferFailure = "550 Cannot determine append position.\r\n";

      CloseHandle(file);

      return false;

    }

    offset = static_cast<unsigned long>(size.QuadPart);

    committed = offset;

  }

  if (offset) {

    LARGE_INTEGER size, position;

    position.QuadPart = offset;

    if (!GetFileSizeEx(file, &size) || position.QuadPart > size.QuadPart ||

        !SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {

      transferFailure = "550 Cannot resume beyond the existing file.\r\n";

      CloseHandle(file);

      return false;

    }

  }

#endif

  if (!uploadBuffer) {

    uploadBuffer = static_cast<char*>(malloc(2 * FTP_DATA_BUFFER_SIZE));

    uploadCanPipeline = uploadBuffer != nullptr;

    if (!uploadBuffer) uploadBuffer = static_cast<char*>(malloc(FTP_DATA_BUFFER_SIZE));

  }

  char* allocation = uploadBuffer;

  bool canPipeline = uploadCanPipeline;

  char* buffer = allocation;

#ifdef NXDK

  std::unique_ptr<UploadWriter> writer;

#endif

  bool success = buffer != nullptr;

  if (!success) transferFailure = "451 Not enough memory for upload.\r\n";

  size_t buffered = 0;

  bool eof = false;

  unsigned long lastActivity = GetTickCount();

  int connectionError = 0;

  while (success && !server->isStopping() && !eof) {

    const int count = recv(dataFd, buffer + buffered, FTP_DATA_BUFFER_SIZE - buffered, 0);

    if (count < 0) {

      const int error = errno;

      if (error == EINTR) continue;

      if ((error == EWOULDBLOCK || error == EAGAIN || error == ETIMEDOUT) &&

          static_cast<unsigned long>(GetTickCount() - lastActivity) < 15000) continue;

      connectionError = error;

      transferFailure = "426 Upload connection failed; socket error " + std::to_string(error) +

                        i18n::c(" after ") + std::to_string(committed) + " bytes saved.\r\n";

      success = false;

      break;

    }

    if (count > 0) lastActivity = GetTickCount();

    eof = count == 0;

    buffered += count;

    // TCP receives have arbitrary sizes. Coalesce into full disk writes, with

    // one final short write at EOF, and handle partial disk writes explicitly.

    if (buffered != FTP_DATA_BUFFER_SIZE && !eof) continue;

#ifdef NXDK

    // Small files keep the synchronous path to avoid thread-start overhead.

    if (!writer && canPipeline && !eof && buffered == FTP_DATA_BUFFER_SIZE) {

      writer.reset(new UploadWriter(file, committed));

      if (!writer->available()) { writer.reset(); canPipeline = false; }

    }

    if (writer) {

      if (buffered && !writer->submit(buffer, buffered)) {

        writer->getFailure(transferFailure); success = false; break;

      }

      buffer = buffer == allocation ? allocation + FTP_DATA_BUFFER_SIZE : allocation;

      buffered = 0;

      continue;

    }

#endif

    size_t written = 0;

    while (written < buffered) {

#ifdef NXDK

      DWORD actual = 0;

      const BOOL ok = WriteFile(file, buffer + written, buffered - written, &actual, nullptr);

      const DWORD error = ok ? 0 : GetLastError();

      written += actual;

      committed += actual;

      if (!ok || !actual) {

        transferFailure = "451 Disk write failed; error " + std::to_string(error) +

                          i18n::c(" after ") + std::to_string(committed) + " bytes saved.\r\n";

        success = false;

        break;

      }

#else

      committed += buffered - written;

      written = buffered;

#endif

    }

    buffered = 0;

  }

#ifdef NXDK

  if (writer) {

    // Drain the final queued write before reporting success, freeing memory,

    // closing the destination, or letting the server shut down.

    writer->finish();

    committed = writer->bytesSaved();

    if (writer->getFailure(transferFailure)) success = false;

    else if (connectionError) {

      transferFailure = "426 Upload connection failed; socket error " +

          std::to_string(connectionError) + i18n::c(" after ") +

          std::to_string(committed) + " bytes saved.\r\n";

    }

  }

#endif

  // Session owns the buffers; the writer is drained before they are reused.

#ifdef NXDK

  CloseHandle(file);

#endif

  if (!success) InfoLog::outputLine(InfoLog::ERROR, "%s: %s", filePath.c_str(), transferFailure.c_str());

  return success && eof && !server->isStopping();

}