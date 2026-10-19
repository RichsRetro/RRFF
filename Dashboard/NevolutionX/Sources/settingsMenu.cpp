#include "i18n.hpp"

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

  translatedLabel=i18n::text(valuedLabel);return translatedLabel;

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

  valuedLabel = label + (value ? i18n::c("Yes") : i18n::c("No"));

#endif

}

void togglingEEPROMMenuItem::execute(Menu*) {

#ifdef NXDK

  value = !value;

  valuedLabel = label + (value ? i18n::c("Yes") : i18n::c("No"));

  ULONG Type = 0;

  uint32_t Value = getEEPROMValue<uint32_t>(valueIndex, Type);

  Value = (Value & ~bitmask) + (bitmask * value);

  ExSaveNonVolatileSetting(valueIndex, Type, &Value, sizeof(Value));

#endif

}

std::string_view togglingEEPROMMenuItem::getLabel() const {

  translatedLabel=i18n::text(valuedLabel);return translatedLabel;

}

/******************************************************************************************

 *                               settingsMenu

 *****************************************************************************************/

extern std::string getDvdDiagnostics();

namespace {

class dvdDiagnosticItem : public MenuNonInteractive {

  mutable std::string current;

public:

  dvdDiagnosticItem(MenuNode* parent) : MenuNonInteractive(parent, i18n::c("DVD status details")) {}

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

    displayLabel = std::string(i18n::c("TV text smoothing: ")) +

                   (config.settings.homescreen.getSmoothText() ? i18n::c("ON") : i18n::c("OFF"));

  }

  std::string_view getLabel() const override { translatedLabel=i18n::text(displayLabel);return translatedLabel; }

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

  std::string_view getLabel() const override { translatedLabel=i18n::text(displayLabel);return translatedLabel; }

  void execute(Menu*) override {

    config.settings.ftp.setEnabled(!config.settings.ftp.getEnabled());

    config.setChanged();

    config.storeToDisk();

    updateLabel();

  }

private:

  void updateLabel() {

    displayLabel = std::string(i18n::c("FTP server: ")) +

                   (config.settings.ftp.getEnabled() ? i18n::c("ON") : i18n::c("OFF"));

  }

  Config& config;

  std::string displayLabel;

};

class ftpPortMenuItem : public MenuItem {

public:

  ftpPortMenuItem(MenuNode* parent, Config& config)

      : MenuItem(parent, ""), config(config) { updateLabel(); }

  std::string_view getLabel() const override { translatedLabel=i18n::text(displayLabel);return translatedLabel; }

  void execute(Menu*) override {

    config.settings.ftp.setPort(config.settings.ftp.getPort() == 21 ? 2121 : 21);

    config.setChanged();

    config.storeToDisk();

    updateLabel();

  }

private:

  void updateLabel() {

    displayLabel = std::string(i18n::c("FTP port: ")) +

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

    displayLabel = i18n::c("IP address: ") + networkManager.IPAddressString() + i18n::c(" (read-only)");

    translatedLabel=i18n::text(displayLabel);return translatedLabel;

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

        this, std::string(i18n::c("Username: ")) + config.settings.ftp.getUser() + i18n::c(" (read-only)")));

    addNode(std::make_shared<MenuNonInteractive>(

        this, std::string(i18n::c("Password: ")) + config.settings.ftp.getPassword() + i18n::c(" (read-only)")));

    addNode(std::make_shared<MenuNonInteractive>(

        this, i18n::c("Press A on FTP server or port to change.")));

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

      text = std::string(i18n::c("Automatic network time: ")) +

             (owner.config.settings.sntp.getEnabled() ? i18n::c("ON") : i18n::c("OFF"));

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

      text = owner.result.empty() ? i18n::c("Choose a field, adjust it, then Save date & time. Saving switches automatic time OFF. A missing clock capacitor can lose time after unplugging.") : owner.result;

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

    if(!SystemTimeToFileTime(&draft, &local)) { result = i18n::c("Invalid date. Clock unchanged."); return; }

    TIME_ZONE_INFORMATION zone{};

    if(GetTimeZoneInformation(&zone) == TIME_ZONE_ID_INVALID) {

      result = i18n::c("Could not read clock offset. Clock unchanged."); return;

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

      result = i18n::c("Could not save clock. Try again."); return;

    }

    config.settings.sntp.setEnabled(false);

    config.setChanged(); config.storeToDisk();

    result = i18n::c("Date & time saved. Automatic network time OFF.");

  }

public:

  ManualClockMenu(MenuNode* parent, Config& cfg) : MenuNode(parent, i18n::c("Date & time")), config(cfg) {

    GetLocalTime(&draft);

    addNode(std::make_shared<Automatic>(this));

    const char* names[] = {"Year","Month","Day","Hour (24h)","Minute"};

    for(int i=0;i<5;++i) addNode(std::make_shared<Field>(this,i,names[i]));

    addNode(std::make_shared<ClockAction>(this,i18n::c("Save date & time"),[this]{save();}));

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

namespace {

class DashboardLanguageChoice : public MenuItem {

 Config& config;std::string code;

public:DashboardLanguageChoice(MenuNode* parent,const std::string& label,const std::string& code,Config& config):MenuItem(parent,label),config(config),code(code){}

 void execute(Menu* menu)override{config.settings.uiLanguage=code;i18n::select(code);config.setChanged();config.storeToDisk();menu->setCurrentMenu(parentNode->getParent());}

};

class DashboardLanguageMenu : public MenuNode {

public:DashboardLanguageMenu(MenuNode* parent,Config& config):MenuNode(parent,i18n::c("Dashboard language")){

 const char* codes[]={"auto","en","de","es","fr","it","pt"};const char* names[]={"System default","English","Deutsch","Español","Français","Italiano","Português"};

 for(int i=0;i<7;i++)addNode(std::make_shared<DashboardLanguageChoice>(this,names[i],codes[i],config));
 addNode(std::make_shared<MenuNonInteractive>(this,i18n::c("This changes dashboard menus only. Console game language is separate.")));
 addNode(std::make_shared<MenuNonInteractive>(this,i18n::c("Restart XDASH to apply this language everywhere.")));

 }

};

}

void settingsMenu::init() {

  addNode(std::make_shared<DashboardLanguageMenu>(this,config));

  addNode(std::make_shared<ftpSettingsMenu>(this, i18n::c("FTP settings"), config, networkManager));

  addNode(std::make_shared<FileExplorerDirectory>(this, i18n::c("File Explorer"), ""));

  addNode(std::make_shared<videoMenu>(this, i18n::c("Video")));

  addNode(std::make_shared<textSmoothingMenuItem>(this, config));

  addNode(std::make_shared<audioMenu>(this, i18n::c("Console game audio")));

  addNode(std::make_shared<LangMenu>(this, i18n::c("Console game language")));

#ifdef NXDK

  addNode(std::make_shared<ManualClockMenu>(this, config));

#endif

  addNode(std::make_shared<logViewerMenu>(this, i18n::c("Log viewer")));

  addNode(std::make_shared<MenuExec>(i18n::c("Wipe cache partitions"), [](Menu*) { wipe_cache(); }));

  addNode(std::make_shared<dvdDiagnosticItem>(this));

  ;

}

