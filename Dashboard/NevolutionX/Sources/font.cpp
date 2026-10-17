#include "font.hpp"
#include <cassert>
#include <cmath>
#include <algorithm>
#include "3rdparty/SDL_FontCache/SDL_FontCache.h"
#include "infoLog.hpp"
#ifdef NXDK
#include <hal/video.h>
#endif

Font::Font(Renderer& renderer, const char* path) : renderer(renderer) {
  fontPath = path;
  fcFont = FC_CreateFont();
  assert(fcFont);
  bool load_success = FC_LoadFont(fcFont, renderer.getRenderer(), path, 20,
                                  FC_MakeColor(200, 200, 200, 255), TTF_STYLE_NORMAL);
  assert(load_success);
}

Font::~Font() {
  for (auto& item : nativeFonts) FC_FreeFont(item.second);
  FC_FreeFont(fcFont);
}

std::pair<float, float> Font::draw(const std::string& str,
                                   std::pair<float, float> coordinates) {
  FC_Draw(activeFont(), renderer.getRenderer(), std::get<0>(coordinates),
          std::get<1>(coordinates), "%s", str.c_str());
  FC_Rect rect = FC_GetBounds(activeFont(), std::get<0>(coordinates), std::get<1>(coordinates),
                              FC_ALIGN_LEFT, FC_MakeScale(1.0f, 1.0f), "%s", str.c_str());
  return std::pair<float, float>(rect.w, rect.h);
}

std::pair<float, float> Font::drawScaled(const std::string& str,
                                        std::pair<float, float> coordinates,
                                        float scale) {
  if (smoothingEnabled) {
    FC_Font* font = nativeFont(scale);
    FC_Rect rect = FC_Draw(font, renderer.getRenderer(),
                           std::round(coordinates.first), std::round(coordinates.second),
                           "%s", str.c_str());
    return { rect.w, rect.h };
  }
  FC_Rect rect = FC_DrawScale(fcFont, renderer.getRenderer(),
                              std::get<0>(coordinates), std::get<1>(coordinates),
                              FC_MakeScale(scale, scale), "%s", str.c_str());
  return std::pair<float, float>(rect.w, rect.h);
}

std::pair<float, float> Font::drawColumn(const std::string& str,
                                         std::pair<float, float> coordinates,
                                         float maxWidth) {
  FC_Rect rect = FC_DrawColumn(activeFont(), renderer.getRenderer(), std::get<0>(coordinates),
                               std::get<1>(coordinates), static_cast<Uint16>(maxWidth),
                               "%s", str.c_str());

  return std::pair<float, float>{ rect.w, rect.h };
}

std::pair<float, float> Font::drawColumnScaled(const std::string& str,
                                               std::pair<float, float> coordinates,
                                               float maxWidth, float scale) {
  if (smoothingEnabled) {
    FC_Font* font = nativeFont(scale);
    FC_Rect rect = FC_DrawColumn(font, renderer.getRenderer(),
        std::round(coordinates.first), std::round(coordinates.second),
        static_cast<Uint16>(maxWidth), "%s", str.c_str());
    return { rect.w, rect.h };
  }
  FC_Rect rect = FC_DrawColumnScale(fcFont, renderer.getRenderer(),
                                    std::get<0>(coordinates), std::get<1>(coordinates),
                                    static_cast<Uint16>(maxWidth),
                                    FC_MakeScale(scale, scale), "%s", str.c_str());
  return std::pair<float, float>{ rect.w, rect.h };
}

float Font::getFontHeight() const {
  return FC_GetLineHeight(activeFont());
}

float Font::getColumnHeight(const std::string& str, float maxWidth) const {
  return FC_GetColumnHeight(activeFont(), static_cast<Uint16>(maxWidth), "%s", str.c_str());
}

float Font::getTextHeight(const std::string& str) const {
  return FC_GetHeight(activeFont(), "%s", str.c_str());
}

float Font::getTextWidth(const std::string& str) const {
  return FC_GetWidth(activeFont(), "%s", str.c_str());
}

void Font::setSmoothing(bool enabled) {
  smoothingEnabled = enabled;
#ifdef NXDK
  // Avoid whole-picture blur and reduce vertical filtering for native-size text.
  // NXDK caches this value, so unchanged frames do not repeat SMBus writes.
  XVideoSetSoftenFilter(FALSE);
  XVideoSetFlickerFilter(enabled ? 1 : 5);
#endif

  FC_SetDefaultColor(fcFont, enabled ? FC_MakeColor(200, 200, 200, 255)
                                    : FC_MakeColor(200, 200, 200, 255));
}
FC_Font* Font::nativeFont(float scale) {
  // Rasterize at the final size: the Xbox SDL software renderer does not
  // apply linear texture sampling to ordinary scaled copies.
  const int points = std::max(8, std::min(40, static_cast<int>(std::round(20.0f * scale))));

  auto found = nativeFonts.find(points);
  if (found != nativeFonts.end()) return found->second;
  FC_Font* font = FC_CreateFont();
  if (!font) return fcFont;
  const auto separator = fontPath.find_last_of("/\\");
  const std::string tvPath = (separator == std::string::npos ? std::string() : fontPath.substr(0, separator + 1)) + "Lato-Bold.ttf";
  if (!FC_LoadFont(font, renderer.getRenderer(), tvPath.c_str(), points,
                   FC_MakeColor(200, 200, 200, 255), TTF_STYLE_NORMAL)) {
    FC_FreeFont(font);
    return fcFont;
  }
  nativeFonts[points] = font;
  return font;
}
FC_Font* Font::activeFont() const {
  return smoothingEnabled ? const_cast<Font*>(this)->nativeFont(1.0f) : fcFont;
}
std::pair<float, float> Font::drawMenuText(const std::string& str,
    std::pair<float, float> coordinates, float maxWidth, float scale, bool readOnly, bool selected) {
  FC_Font* font = smoothingEnabled ? nativeFont(scale) : fcFont;
  // Visibility test: opaque black outline in every direction, for all menu rows.
  // Compensate for renderer scaling so the radius is one output pixel.
  float outputScaleX = 1.0f, outputScaleY = 1.0f;
  SDL_RenderGetScale(renderer.getRenderer(), &outputScaleX, &outputScaleY);
  if (outputScaleX <= 0.0f) outputScaleX = 1.0f;
  if (outputScaleY <= 0.0f) outputScaleY = 1.0f;
  FC_SetDefaultColor(font, FC_MakeColor(0, 0, 0, 255));
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      if (x == 0 && y == 0) continue;
      drawColumnScaled(str,
          {coordinates.first + x / outputScaleX, coordinates.second + y / outputScaleY},
          maxWidth, scale);
    }
  }
  const Uint8 shade = selected ? (readOnly ? 176 : 200) : 220;
  FC_SetDefaultColor(font, FC_MakeColor(shade, shade, shade, 255));
  auto dimensions = drawColumnScaled(str, coordinates, maxWidth, scale);
  FC_SetDefaultColor(font, FC_MakeColor(200, 200, 200, 255));
  return dimensions;
}
std::pair<float,float> Font::measureMenuText(const std::string& str, float maxWidth, float scale) {
  FC_Font* font = smoothingEnabled ? nativeFont(scale) : fcFont;
  const float factor = smoothingEnabled ? 1.0f : scale;
  return {FC_GetWidth(font,"%s",str.c_str()) * factor,
          FC_GetColumnHeight(font,static_cast<Uint16>(std::min(65535.0f,maxWidth/factor)),
                             "%s",str.c_str())};
}