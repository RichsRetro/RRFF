#include "settingsMenu.hpp"
#include "audioMenu.hpp"
#include "eeprom.hpp"
#include "fileExplorer.hpp"
#include "langMenu.hpp"
#include "logViewerMenu.hpp"
#include "networkManager.hpp"
#include "sntpClient.hpp"
#include <algorithm>
#include <functional>
#include "videoMenu.hpp"
#include "wipeCache.hpp"

#ifdef NXDK
#include <xboxkrnl/xboxkrnl.h>
#include <windows.h>
#endif

/******************************************************************************************
 *                              switchingMenuItem
 *****************************************************************************************/
switchingMenuItem::switchingMenuItem(std::string const& label, std::vector<std::string> s) :
    MenuItem(label), values(s) {
}

std::string_view switchingMenuItem::getLabel() const {
  return valuedLabel;
}

/******************************************************************************************
 *                              togglingEEPROMMenuItem
 *****************************************************************************************/
togglingEEPROMMenuItem::togglingEEPROMMenuItem(std::string const& label,
                                               uint32_t vI,
                                               uint32_t bmask) :
    MenuItem(label), bitmask(bmask), valueIndex(vI) {

#ifdef NXDK
  uint32_t Value = getEEPROMValue<uint32_t>(valueIndex);
  value = (Value & bitmask) != 0;

  valuedLabel = label + (value ? "Yes" : "No");
#endif
}

void togglingEEPROMMenuItem::execute(Menu*) {
#ifdef NXDK
  value = !value;
  valuedLabel = label + (value ? "Yes" : "No");

  ULONG Type = 0;
  uint32_t Value = getEEPROMValue<uint32_t>(valueIndex, Type);
  Value = (Value & ~bitmask) + (bitmask * value);
  ExSaveNonVolatileSetting(valueIndex, Type, &Value, sizeof(Value));
#endif
}

std::string_view togglingEEPROMMenuItem::getLabel() const {
  return valuedLabel;
}

/******************************************************************************************
 *                               settingsMenu
 *****************************************************************************************/
extern std::string getDvdDiagnostics();
namespace {
class dvdDiagnosticItem : public MenuNonInteractive {
  mutable std::string current;
public:
  dvdDiagnosticItem(MenuNode* parent) : MenuNonInteractive(parent, "DVD status details") {}
  std::string_view getLabel() const override {
    current = getDvdDiagnostics();
    return current;
  }
};
class textSmoothingMenuItem : public MenuItem {
  Config& config;
  std::string displayLabel;
public:
  textSmoothingMenuItem(MenuNode* parent, Config& config)
      : MenuItem(parent, ""), config(config) { updateLabel(); }
  void updateLabel() {
    displayLabel = std::string("TV text smoothing: ") +
                   (config.settings.homescreen.getSmoothText() ? "ON" : "OFF");
  }
  std::string_view getLabel() const override { return displayLabel; }
  void execute(Menu*) override {
    config.settings.homescreen.setSmoothText(!config.settings.homescreen.getSmoothText());
    config.setChanged();
    config.storeToDisk();
    updateLabel();
  }
};
class ftpEnabledMenuItem : public MenuItem {
public:
  ftpEnabledMenuItem(MenuNode* parent, Config& config)
      : MenuItem(parent, ""), config(config) { updateLabel(); }

  std::string_view getLabel() const override { return displayLabel; }

  void execute(Menu*) override {
    config.settings.ftp.setEnabled(!config.settings.ftp.getEnabled());
    config.setChanged();
    config.storeToDisk();
    updateLabel();
  }

private:
  void updateLabel() {
    displayLabel = std::string("FTP server: ") +
                   (config.settings.ftp.getEnabled() ? "ON" : "OFF");
  }

  Config& config;
  std::string displayLabel;
};

class ftpPortMenuItem : public MenuItem {
public:
  ftpPortMenuItem(MenuNode* parent, Config& config)
      : MenuItem(parent, ""), config(config) { updateLabel(); }

  std::string_view getLabel() const override { return displayLabel; }

  void execute(Menu*) override {
    config.settings.ftp.setPort(config.settings.ftp.getPort() == 21 ? 2121 : 21);
    config.setChanged();
    config.storeToDisk();
    updateLabel();
  }

private:
  void updateLabel() {
    displayLabel = std::string("FTP port: ") +
                   std::to_string(config.settings.ftp.getPort());
  }

  Config& config;
  std::string displayLabel;
};

class ftpIpMenuItem : public MenuNonInteractive {
public:
  ftpIpMenuItem(MenuNode* parent, NetworkManager& networkManager)
      : MenuNonInteractive(parent, ""), networkManager(networkManager) {}

  std::string_view getLabel() const override {
    displayLabel = "IP address: " + networkManager.IPAddressString() + " (read-only)";
    return displayLabel;
  }

private:
  NetworkManager& networkManager;
  mutable std::string displayLabel;
};

class ftpSettingsMenu : public MenuNode {
public:
  ftpSettingsMenu(MenuNode* parent, const std::string& label, Config& config,
                  NetworkManager& networkManager)
      : MenuNode(parent, label) {
    addNode(std::make_shared<ftpEnabledMenuItem>(this, config));
    addNode(std::make_shared<ftpPortMenuItem>(this, config));
    addNode(std::make_shared<ftpIpMenuItem>(this, networkManager));
    addNode(std::make_shared<MenuNonInteractive>(
        this, std::string("Username: ") + config.settings.ftp.getUser() + " (read-only)"));
    addNode(std::make_shared<MenuNonInteractive>(
        this, std::string("Password: ") + config.settings.ftp.getPassword() + " (read-only)"));
    addNode(std::make_shared<MenuNonInteractive>(
        this, "Press A on FTP server or port to change."));
  }
};
}

settingsMenu::settingsMenu(MenuNode* parent, std::string const& label, Config& config,
                           NetworkManager& networkManager) :
    MenuNode(parent, label), config(config), networkManager(networkManager) {
  init();
}

#ifdef NXDK
namespace {
class ClockAction : public MenuItem {
  std::function<void()> action;
public:
  ClockAction(MenuNode* parent, const std::string& text, std::function<void()> fn)
      : MenuItem(parent, text), action(std::move(fn)) {}
  void execute(Menu*) override { action(); }
};
class ManualClockMenu : public MenuNode {
  Config& config;
  SYSTEMTIME draft{};
  std::string result;
  class Field : public MenuNode {
    ManualClockMenu& owner;
    int id;
    mutable std::string text;
  public:
    Field(ManualClockMenu* parent, int index, const char* name)
        : MenuNode(parent, name), owner(*parent), id(index) {
      addNode(std::make_shared<ClockAction>(this, "Increase", [this]{ owner.adjust(id, 1); }));
      addNode(std::make_shared<ClockAction>(this, "Decrease", [this]{ owner.adjust(id, -1); }));
    }
    std::string_view getLabel() const override {
      text = label + ": " + std::to_string(owner.value(id));
      return text;
    }
  };
  class Automatic : public MenuItem {
    ManualClockMenu& owner;
    mutable std::string text;
  public:
    Automatic(ManualClockMenu* parent) : MenuItem(parent, ""), owner(*parent) {}
    std::string_view getLabel() const override {
      text = std::string("Automatic network time: ") +
             (owner.config.settings.sntp.getEnabled() ? "ON" : "OFF");
      return text;
    }
    void execute(Menu*) override {
      bool enabled = !owner.config.settings.sntp.getEnabled();
      owner.config.settings.sntp.setEnabled(enabled);
      sntpClient::setAutomaticEnabled(enabled);
      owner.config.setChanged(); owner.config.storeToDisk();
    }
  };
  class Status : public MenuNonInteractive {
    ManualClockMenu& owner;
    mutable std::string text;
  public:
    Status(ManualClockMenu* parent) : MenuNonInteractive(parent, ""), owner(*parent) {}
    std::string_view getLabel() const override {
      text = owner.result.empty() ? "Choose a field, adjust it, then Save date & time. Saving switches automatic time OFF. A missing clock capacitor can lose time after unplugging." : owner.result;
      return text;
    }
  };
  int days() const {
    static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int n = lengths[draft.wMonth - 1];
    if (draft.wMonth == 2 && draft.wYear % 4 == 0 &&
        (draft.wYear % 100 != 0 || draft.wYear % 400 == 0)) ++n;
    return n;
  }
  int value(int id) const {
    switch(id) { case 0: return draft.wYear; case 1: return draft.wMonth;
      case 2: return draft.wDay; case 3: return draft.wHour; default: return draft.wMinute; }
  }
  void adjust(int id, int delta) {
    WORD* fields[] = {&draft.wYear,&draft.wMonth,&draft.wDay,&draft.wHour,&draft.wMinute};
    const int low[] = {2000,1,1,0,0};
    const int high[] = {2099,12,days(),23,59};
    int next = value(id) + delta;
    if(next < low[id]) next = high[id];
    if(next > high[id]) next = low[id];
    *fields[id] = static_cast<WORD>(next);
    draft.wDay = std::min<int>(draft.wDay, days());
    result.clear();
  }
  void save() {
    draft.wSecond = 0; draft.wMilliseconds = 0;
    FILETIME local{};
    if(!SystemTimeToFileTime(&draft, &local)) { result = "Invalid date. Clock unchanged."; return; }
    TIME_ZONE_INFORMATION zone{};
    if(GetTimeZoneInformation(&zone) == TIME_ZONE_ID_INVALID) {
      result = "Could not read clock offset. Clock unchanged."; return;
    }
    LARGE_INTEGER utc{};
    utc.LowPart = local.dwLowDateTime; utc.HighPart = local.dwHighDateTime;
    // Match NXDK GetLocalTime, which uses the console's base offset without DST.
    utc.QuadPart += static_cast<LONGLONG>(zone.Bias) * 60LL * 10000000LL;
    const bool wasAutomatic = config.settings.sntp.getEnabled();
    sntpClient::setAutomaticEnabled(false);
    NTSTATUS status = NtSetSystemTime(&utc, nullptr);
    if(!NT_SUCCESS(status)) {
      sntpClient::setAutomaticEnabled(wasAutomatic);
      result = "Could not save clock. Try again."; return;
    }
    config.settings.sntp.setEnabled(false);
    config.setChanged(); config.storeToDisk();
    result = "Date & time saved. Automatic network time OFF.";
  }
public:
  ManualClockMenu(MenuNode* parent, Config& cfg) : MenuNode(parent, "Date & time"), config(cfg) {
    GetLocalTime(&draft);
    addNode(std::make_shared<Automatic>(this));
    const char* names[] = {"Year","Month","Day","Hour (24h)","Minute"};
    for(int i=0;i<5;++i) addNode(std::make_shared<Field>(this,i,names[i]));
    addNode(std::make_shared<ClockAction>(this,"Save date & time",[this]{save();}));
    addNode(std::make_shared<Status>(this));
  }
  void execute(Menu* menu) override {
    if(menu->getCurrentMenu() != this) {
      GetLocalTime(&draft);
      if(draft.wYear < 2000 || draft.wYear > 2099) draft.wYear = 2026;
      if(draft.wMonth < 1 || draft.wMonth > 12) draft.wMonth = 1;
      draft.wDay = std::max(1,std::min<int>(draft.wDay,days()));
      result.clear();
    }
    MenuNode::execute(menu);
  }
};
}
#endif
void settingsMenu::init() {
  addNode(std::make_shared<ftpSettingsMenu>(this, "FTP settings", config, networkManager));
  addNode(std::make_shared<FileExplorerDirectory>(this, "File Explorer", ""));
  addNode(std::make_shared<videoMenu>(this, "Video"));
  addNode(std::make_shared<textSmoothingMenuItem>(this, config));
  addNode(std::make_shared<audioMenu>(this, "Console game audio"));
  addNode(std::make_shared<LangMenu>(this, "Console game language"));
#ifdef NXDK
  addNode(std::make_shared<ManualClockMenu>(this, config));
#endif
  addNode(std::make_shared<logViewerMenu>(this, "Log viewer"));

  addNode(std::make_shared<MenuExec>("Wipe cache partitions", [](Menu*) { wipe_cache(); }));
  addNode(std::make_shared<dvdDiagnosticItem>(this));
  ;
}
