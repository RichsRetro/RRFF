#ifndef RICH_RETRO_DVD_COPY_HPP

#define RICH_RETRO_DVD_COPY_HPP

#include "menu.hpp"

class Font;

class Renderer;

typedef void (*DvdCopyProgressCallback)(Menu&, Renderer&, Font&, double, const std::string&, const std::string&, bool);

void setDvdCopyProgressCallback(DvdCopyProgressCallback callback, Font& font);

#ifdef NXDK

void lockDvdAccess();

void unlockDvdAccess();

void stopDvdCopyPreflight();

void pauseDvdStatusPolling();

void resumeDvdStatusPolling();

#endif

class DvdCopyMenu : public MenuNode {

public:

  explicit DvdCopyMenu(MenuNode* parent, const std::string& label);

  void execute(Menu* menu) override;

private:

  void loadTargets();

  bool loaded{ false };

};

#endif

