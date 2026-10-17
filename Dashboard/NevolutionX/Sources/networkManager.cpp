#include "networkManager.hpp"
#include <utility>

#ifdef NXDK

#include "networking.hpp"

extern "C" {
extern struct netif* g_pnetif;
void nvnetdrv_stop(void);
}

#endif

NetworkManager::NetworkManager(Config config) :
    config(std::move(config)), status(INIT_NONE) {
}

void NetworkManager::asyncInit() {
  assert(status == INIT_NONE);
  init_thread = std::thread(thread_main, this);
}

void NetworkManager::thread_main(NetworkManager* manager) {
#ifdef NXDK
  const netConfig& net = manager->config.settings.net;

  staticIP static_ip;
  static_ip.gateway.addr = net.getStaticGateway();
  static_ip.ip.addr = net.getStaticIP();
  static_ip.netmask.addr = net.getStaticNetmask();
  static_ip.dns1.addr = net.getStaticDns1();
  static_ip.dns2.addr = net.getStaticDns2();

  int status = setupNetwork(net.getUseDHCP(), static_ip);
  if (status) {
    manager->status = INIT_FAILED;
  } else {
    manager->status = INIT_SUCCEEDED;
  }

#else
  manager->status = INIT_FAILED;
#endif
}

void NetworkManager::pollConnection() {
#ifdef NXDK
  // nxNetInit returns -2 after its startup DHCP wait, but leaves lwIP and
  // the Ethernet driver's link-change callbacks alive. A later DHCP lease
  // must promote the existing interface rather than creating another stack.
  if (status == INIT_FAILED && hasValidAddress()) status = INIT_SUCCEEDED;
#endif
}

std::string NetworkManager::IPAddressString() const {
#ifdef NXDK
  if (g_pnetif && !netif_is_link_up(g_pnetif)) return "Ethernet unplugged";
  NetworkStatus current_status = status;
  if (current_status != INIT_SUCCEEDED && current_status != RUNNING) {
    return current_status == INIT_FAILED ? "Network failed" : "Starting network";
  }

  if (!g_pnetif) {
    return "Waiting for network";
  }

  const ip4_addr_t* address = netif_ip4_addr(g_pnetif);
  if (!address || ip4_addr_isany(address)) {
    return "Waiting for DHCP";
  }
  return ip4addr_ntoa(address);
#else
  return "Not supported";
#endif
}

bool NetworkManager::hasValidAddress() const {
#ifdef NXDK
  if (!g_pnetif || !netif_is_link_up(g_pnetif)) return false;
  const ip4_addr_t* address = netif_ip4_addr(g_pnetif);
  return address && !ip4_addr_isany(address);
#else
  return false;
#endif
}

void NetworkManager::stopForLaunch() {
  // Initialization can still be in progress if a title was selected immediately.
  if (init_thread.joinable()) init_thread.join();
#ifdef NXDK
  const auto current = status.load();
  if (g_pnetif && current != INIT_NONE) {
    nvnetdrv_stop();
    status = INIT_NONE;
  }
#endif
}