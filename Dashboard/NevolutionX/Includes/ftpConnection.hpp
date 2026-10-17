#ifndef __FTPCONNECTION_H
#define __FTPCONNECTION_H

// Set buffer sizes to 1KiB for commands and 64KiB for data
// Command buffer should selldom (never?) exceed 512B but I prefer a little headroom.
// Data buffer is bigger in order for disk writes to be quicker - writing bigger
// chunks is usually preferable for a myriad of reasons.
#define FTP_CMD_BUFFER_SIZE (1024)
#define FTP_DATA_BUFFER_SIZE (256 * 1024)

#include <string>
#include "ftpServer.hpp"

class ftpServer;

class ftpConnection {
  int _fd;
  int dataFd{ -1 };
  int passiveListenFd{ -1 };
  std::string pwd;
  bool logged_in;
  char* buf;
  // Reuse upload memory for this session instead of reallocating per file.
  char* uploadBuffer{nullptr};
  bool uploadCanPipeline{false};
  std::string pendingInput;
  char mode;
  unsigned long restartOffset{0};
  bool appendUpload{false};
  std::string transferFailure;
  std::string rnfr;

  ftpServer* server;

  void sendStdString(std::string const& s, int flags);
  void sendStdString(int fd, std::string const& s, int flags);
  //  void handleCommand();

  void sendFolderContents(int fd, std::string& path);
  bool sendFile(std::string const& fileName);
  bool recvFile(std::string const& fileName);

  std::string unixToDosPath(std::string const& path);

  //  void cmdAbor();
  void cmdCwd(std::string const& arg);
  void cmdCdup();
  void cmdDele(std::string const& arg);
  void cmdList(std::string const& arg);
  //  void cmdMdtm(std::string const& arg);
  void cmdMkd(std::string const& arg);
  void cmdNlst(std::string const& arg);
  void cmdPass(std::string const& arg);
  //  void cmdPasv(std::string const& arg);
  void cmdPort(std::string const& arg);
  void cmdEprt(std::string const& arg);
  void cmdPasv();
  void cmdEpsv();
  bool prepareDataConnection();
  bool sendAll(int fd, const char* bytes, size_t length, int flags = 0);
  void setTransferTimeouts(int fd);
  void closeDataConnection();
  void cmdPwd();
  //  void cmdQuit();
  void cmdRetr(std::string const& arg);
  void cmdRmd(std::string const& arg);
  void cmdRnfr(std::string const& arg);
  void cmdRnto(std::string const& arg);
  //  void cmdSite(std::string const& arg);
  //  void cmdSize(std::string const& arg);
  void cmdStor(std::string const& arg);
  void cmdSyst();
  void cmdType(std::string const& arg);
  void cmdUser(std::string const& arg);
  void cmdExecute(std::string const& path);
  void cmdUnimplemented(std::string const& arg);

public:
  ftpConnection(int fd, ftpServer* s);
  ~ftpConnection();
  bool update();
};

#endif
