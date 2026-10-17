#ifndef FONT_H
#define FONT_H

#include <string>
#include <map>
#include <utility>
#include <vector>
#include "3rdparty/SDL_FontCache/SDL_FontCache.h"
#include "renderer.hpp"

class Font {
private:
  FC_Font* fcFont;
  std::string fontPath;
  bool smoothingEnabled = false;
  std::map<int, FC_Font*> nativeFonts;
  FC_Font* nativeFont(float scale);
  FC_Font* activeFont() const;
  Renderer& renderer;

public:
  Font(Renderer& renderer, const char* path);
  ~Font();
  void setSmoothing(bool enabled);
  std::pair<float, float> drawMenuText(const std::string& str,
      std::pair<float, float> coordinates, float maxWidth, float scale, bool readOnly, bool selected = false);

  std::pair<float, float> draw(const std::string& str, std::pair<float, float> coordinates);
  std::pair<float, float> drawScaled(const std::string& str,
                                    std::pair<float, float> coordinates, float scale);
  std::pair<float, float> drawColumn(const std::string& str,
                                     std::pair<float, float> coordinates,
                                     float maxWidth);
  std::pair<float, float> drawColumnScaled(const std::string& str,
                                           std::pair<float, float> coordinates,
                                           float maxWidth, float scale);

  std::pair<float, float> measureMenuText(const std::string& str, float maxWidth, float scale);
  float getFontHeight() const;
  float getColumnHeight(const std::string& str, float maxWidth) const;
  float getTextHeight(const std::string& str) const;
  float getTextWidth(const std::string& str) const;
};

#endif
