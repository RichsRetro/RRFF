#include "i18n.hpp"

#include "audioMenu.hpp"

#include "eeprom.hpp"

#ifdef NXDK

#include <xboxkrnl/xboxkrnl.h>

#endif

audioChannelsMenuItem::audioChannelsMenuItem() :

    switchingMenuItem(i18n::c("Channels"), { i18n::c("Stereo"), i18n::c("Mono"), i18n::c("Surround") }) {

#ifdef NXDK

  uint32_t Value = getEEPROMValue<uint32_t>(valueIndex);

  selected = Value & 0x00000003;

  if (selected >= values.size()) selected = 0;

  valuedLabel = i18n::c("Channels: ") + values.at(selected);

#endif

}

void audioChannelsMenuItem::execute(Menu*) {

#ifdef NXDK

  selected = (++selected) % values.size();

  valuedLabel = label + values.at(selected);

  ULONG Type = 0;

  uint32_t Value = getEEPROMValue<uint32_t>(valueIndex, Type);

  Value = (Value & 0xFFFFFFF0) + selected;

  ExSaveNonVolatileSetting(valueIndex, Type, &Value, sizeof(Value));

#endif

}

audioMenu::audioMenu(MenuNode* parent, std::string const& label) : MenuNode(parent, label) {

  addNode(std::make_shared<audioChannelsMenuItem>());

  addNode(

      std::make_shared<togglingEEPROMMenuItem>(i18n::c("Dolby Digital enabled: "), 0x9, 0x00010000));

  addNode(std::make_shared<togglingEEPROMMenuItem>(i18n::c("DTS enabled: "), 0x9, 0x00020000));

  addNode(std::make_shared<MenuNonInteractive>(this, i18n::c("These settings are used by games. XDASH does not currently play menu sounds.")));

}

