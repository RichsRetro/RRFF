#include "i18n.hpp"

#include "videoMenu.hpp"

#include "eeprom.hpp"

#ifdef NXDK

#include <xboxkrnl/xboxkrnl.h>

#endif

static const uint32_t widescreen = 0x00010000;

static const uint32_t letterbox = 0x00100000;

static const std::vector<uint32_t> ratios = { 0, widescreen, letterbox };

videoRatioMenuItem::videoRatioMenuItem() :

    switchingMenuItem(i18n::c("Screen Ratio: "), { i18n::c("Normal"), i18n::c("Widescreen"), i18n::c("Letterbox") }) {

#ifdef NXDK

  uint32_t Value = getEEPROMValue<uint32_t>(valueIndex);

  selected = Value & 0x00110000;

  if (selected & widescreen) {

    selected = 1;

  } else if (selected & letterbox) {

    selected = 2;

  }

  valuedLabel = label + values.at(selected);

#endif

}

void videoRatioMenuItem::execute(Menu* menu) {

#ifdef NXDK

  selected = (++selected) % values.size();

  valuedLabel = label + values.at(selected);

  ULONG Type = 0;

  uint32_t Value = getEEPROMValue<uint32_t>(valueIndex, Type);

  Value = (Value & 0xFFEEFFFF) + ratios.at(selected);

  ExSaveNonVolatileSetting(valueIndex, Type, &Value, sizeof(Value));

  menu->getRenderer().setWidescreen(selected == 1);

#endif

}

videoMenu::videoMenu(MenuNode* parent, std::string const& label) : MenuNode(parent, label) {

  addNode(std::make_shared<videoRatioMenuItem>());

  addNode(std::make_shared<togglingEEPROMMenuItem>(i18n::c("480p (game/boot): "), 0x8, 0x00080000));

  addNode(std::make_shared<togglingEEPROMMenuItem>(i18n::c("720p (game/boot): "), 0x8, 0x00020000));

  addNode(std::make_shared<togglingEEPROMMenuItem>(i18n::c("1080i (game/boot): "), 0x8, 0x00040000));

  addNode(std::make_shared<MenuNonInteractive>(this, i18n::c("Screen ratio changes the dash layout immediately. HD modes require a compatible cable and affect game/boot output.")));

}

