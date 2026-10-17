#include "ftpServer.hpp"
#include <algorithm>
#include "infoLog.hpp"

#ifdef NXDK
#include <lwip/arch.h>
#include <lwip/debug.h>
#include <lwip/dhcp.h>
#include <lwip/errno.h>
#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/opt.h>
#include <lwip/sys.h>
#include <lwip/tcpip.h>
#include <lwip/timeouts.h>
#include <netif/etharp.h>
#include <nxdk/mount.h>
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
#endif


// get sockaddr, IPv4 or IPv6:
void* ftpServer::getInAddr(struct sockaddr* sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

#ifdef NXDK
const char* gai_strerror(int errc) {
  switch (errc) {
  case 200:
    return "EAI_NONAME";
  case 201:
    return "EAI_SERVICE";
  case 202:
    return "EAI_FAIL";
  case 203:
    return "EAI_MEMORY";
  case 204:
    return "EAI_FAMILY";
  case 210:
    return "HOST_NOT_FOUND";
  case 211:
    return "NO_DATA";
  case 212:
    return "NO_RECOVERY";
  case 213:
    return "TRY_AGAIN";
  default:
    return "ERR_OK";
  }
}
#endif

std::string sock_strerror(int errc) {
  switch (errc) {
  case EACCES:
    return "EACCES";
  case EAFNOSUPPORT:
    return "EAFNOSUPPORT";
  case EINVAL:
    return "EINVAL";
  case ENFILE:
    return "ENFILE";
  case EMFILE:
    return "EMFILE";
  case ENOBUFS:
    return "ENOBUFS";
  case ENOMEM:
    return "ENOMEM";
  case EPROTONOSUPPORT:
    return "EPROTONOSUPPORT";
  default:
    return "ERR_OK";
  }
}

ftpServer::ftpServer(ftpConfig const* conf) : conf(conf) {
  FD_ZERO(&master); // clear the master and temp sets
  FD_ZERO(&readFds);

  // Set hints for our future socket(s)
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
}

ftpServer::~ftpServer() {
  stop();
  if (serverThread.joinable()) {
    serverThread.join();
  }
}

int ftpServer::init() {
  int yes = 1;
  if ((i = getaddrinfo(NULL, std::to_string(conf->getPort()).c_str(), &hints, &ai)) != 0) {
    InfoLog::outputLine(InfoLog::ERROR, "Error: selectserver\n");
    return 5;
  }

  /* Get network settings */
  for (p = ai; p != NULL; p = p->ai_next) {
    listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (listener < 0) {
      continue;
    }

    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

    if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
      close(listener);
      continue;
    }
    break;
  }

  freeaddrinfo(ai);

  if (p == NULL) {
    InfoLog::outputLine(InfoLog::ERROR, "Error: selectServer: failed to bind\n");
    return 1;
  }

  /* Start listening */
  if (listen(listener, 10) == -1) {
    InfoLog::outputLine(InfoLog::ERROR, "Error: Listen\n");
    return 2;
  }
  FD_SET(listener, &master);
  fdmax = listener;
  return 0;
}

int ftpServer::run() {
  // A bulk transfer must not occupy the listener or another client's command
  // connection. Give each session its own worker, with a bounded client count.
  while (!stopRequested.load()) {
    for (auto it = workers.begin(); it != workers.end();) {
      if ((*it)->finished.load()) {
        (*it)->thread.join();
        it = workers.erase(it);
      } else {
        ++it;
      }
    }
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(listener, &readable);
    timeval timeout = {};
    timeout.tv_usec = 250000;
    const int ready = select(listener + 1, &readable, nullptr, nullptr, &timeout);
    if (ready < 0) {
      if (errno == EINTR) continue;
      InfoLog::outputLine(InfoLog::ERROR, "FTP listener select failed: %d\n", errno);
      break;
    }
    if (!ready) continue;
    sockaddr_storage peer = {};
    socklen_t length = sizeof(peer);
    const int fd = accept(listener, reinterpret_cast<sockaddr*>(&peer), &length);
    if (fd < 0) continue;
    // 8 command sessions plus their data sockets fit the 32-socket network limit.
    if (workers.size() >= 8) {
      const char busy[] = "421 Too many FTP sessions; retry shortly.\r\n";
      send(fd, busy, sizeof(busy) - 1, 0);
      close(fd);
      continue;
    }
    std::unique_ptr<ClientWorker> worker(new ClientWorker());
    ClientWorker* state = worker.get();
    state->thread = std::thread([this, fd, state]() {
      {
        ftpConnection connection(fd, this);
        while (!stopRequested.load()) {
          fd_set commands;
          FD_ZERO(&commands);
          FD_SET(fd, &commands);
          timeval wait = {};
          wait.tv_usec = 250000;
          const int count = select(fd + 1, &commands, nullptr, nullptr, &wait);
          if (count < 0) {
            if (errno == EINTR) continue;
            break;
          }
          if (count > 0 && !connection.update()) break;
        }
      } // Close the data connection before releasing the command socket.
      close(fd);
      state->finished.store(true);
    });
    workers.push_back(std::move(worker));
  }
  stopRequested.store(true);
  for (auto& worker : workers) {
    if (worker->thread.joinable()) worker->thread.join();
  }
  workers.clear();
  if (listener >= 0) { close(listener); listener = -1; }
  return 0;
}

int ftpServer::openConnection(std::string const& addr, std::string const& port) {
  // Address results belong to this request: multiple FTP workers may connect
  // at once, so never overwrite the listener's shared addrinfo pointers.
  addrinfo connectionHints = {};
  connectionHints.ai_family = AF_INET;
  connectionHints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses = nullptr;
  const int result = getaddrinfo(addr.c_str(), port.c_str(), &connectionHints, &addresses);
  if (result != 0) return -1;
  int fd = -1;
  for (auto* address = addresses; address; address = address->ai_next) {
    fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (fd < 0) continue;
    if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(addresses);
  return fd;
}
void ftpServer::runAsync() {
  stopRequested.store(false);
  serverThread = std::thread(thread_runner, this);
}

void ftpServer::stop() {
  stopRequested.store(true);
}

int ftpServer::thread_runner(ftpServer* server) {
  return server->run();
}
