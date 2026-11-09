/*
  Created by Fabrizio Di Vittorio (fdivitto2013@gmail.com) - <http://www.fabgl.com>
  Copyright (c) 2019-2022 Fabrizio Di Vittorio.
  All rights reserved.


* Please contact fdivitto2013@gmail.com if you need a commercial license.


* This library and related software is available under GPL v3.

  FabGL is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  FabGL is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with FabGL.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "xgraphicsadapter.h"

#include <string.h>




#pragma GCC optimize ("O2")


namespace xpc {



static const RGB222 CGAPalette[16] = {
  RGB222(0, 0, 0),    // black
  RGB222(0, 0, 2),    // blue
  RGB222(0, 2, 0),    // green
  RGB222(0, 2, 2),    // cyan
  RGB222(2, 0, 0),    // red
  RGB222(2, 0, 2),    // magenta
  RGB222(2, 1, 0),    // brown
  RGB222(2, 2, 2),    // light gray
  RGB222(1, 1, 1),    // dark gray
  RGB222(1, 1, 3),    // light blue
  RGB222(1, 3, 1),    // light green
  RGB222(1, 3, 3),    // light cyan
  RGB222(3, 1, 1),    // light red
  RGB222(3, 1, 3),    // light magenta
  RGB222(3, 3, 1),    // yellow
  RGB222(3, 3, 3),    // white
};



static const RGB222 CGAGraphics4ColorsPalette[6][4] = {
  // modo 5 (bit BW do 3D8 ligado): ciano / vermelho / cinza, baixa intensidade
  {
    RGB222(0, 0, 0),
    RGB222(0, 2, 2),    // cyan
    RGB222(2, 0, 0),    // red
    RGB222(2, 2, 2),    // light gray
  },
  // modo 5, alta intensidade
  {
    RGB222(0, 0, 0),
    RGB222(1, 3, 3),    // light cyan
    RGB222(3, 1, 1),    // light red
    RGB222(3, 3, 3),    // white
  },
  // low intensity PC graphics 4 colors palette
  {
    RGB222(0, 0, 0),    // background (not used)
    RGB222(0, 2, 0),    // green
    RGB222(2, 0, 0),    // red
    RGB222(2, 1, 0),    // brown
  },
  // high intensity PC graphics 4 colors palette
  {
    RGB222(0, 0, 0),    // background (not used)
    RGB222(1, 3, 1),    // light green
    RGB222(3, 1, 1),    // light red
    RGB222(3, 3, 1),    // yellow
  },
  // low intensity PC graphics 4 colors alternate palette
  {
    RGB222(0, 0, 0),    // background (not used)
    RGB222(0, 2, 2),    // cyan
    RGB222(2, 0, 2),    // magenta
    RGB222(2, 2, 2),    // light gray
  },
  // low intensity PC graphics 4 colors alternate palette
  {
    RGB222(0, 0, 0),    // background (not used)
    RGB222(1, 3, 3),    // light cyan
    RGB222(3, 1, 3),    // light magenta
    RGB222(3, 3, 3),    // white
  },
};


void GraphicsAdapter::setupEmulation(Emulation emulation)
{
  static const struct {
    bool                 text;
    FontInfo const *     font;                 // valid when text = true
    uint8_t              cursorStart;          // valid when text = true
    uint8_t              cursorEnd;            // valid when text = true
    uint8_t              scanlinesPerCallback;
    DrawScanlineCallback scanlineCallback;
    char const *         modeline;
    int16_t              viewportWidth;
    int16_t              viewportHeight;
  } emulationInfo[] = {

    // Emulation::PC_Text_40x25_16Colors (CGA, Text Mode, 40x25x16)
    {  true,  &FONT_8x8,  5,  7, 4,      drawScanline_PC_Text_40x25_16Colors, QVGA_320x240_60Hz, 320, 200 },

    // Emulation::PC_Text_80x25_16Colors (CGA, Text Mode, 80x25x16)
    {  true, &FONT_8x16, 13, 15, 8,      drawScanline_PC_Text_80x25_16Colors,  VGA_640x480_60Hz, 640, 400 },

    // Emulation::PC_Graphics_320x200_4Colors (CGA, Graphics Mode, 320x200x4)
    { false,    nullptr,  0,  0, 1, drawScanline_PC_Graphics_320x200_4Colors, QVGA_320x240_60Hz, 320, 200 },

    // Emulation::PC_Graphics_640x200_2Colors (CGA, Graphics Mode, 640x200x2)
    { false,    nullptr,  0,  0, 1, drawScanline_PC_Graphics_640x200_2Colors, VGA_640x240_60Hz,  640, 200 },

    // Emulation::PC_Graphics_HGC_720x348 (Hercules, Graphics Mode, 720x348x2)
    { false,    nullptr,  0,  0, 2,     drawScanline_PC_Graphics_HGC_720x348, VGA_720x350_70Hz,  -1,  -1 },

    // Emulation::PC_Graphics_EGA_320x200_16Colors (XPC: EGA, modo 0Dh, 320x200x16)
    { false,    nullptr,  0,  0, 1, drawScanline_PC_Graphics_EGA_320x200_16Colors, QVGA_320x240_60Hz, 320, 200 },

    // Emulation::PC_Graphics_EGA_640x200_16Colors (XPC: EGA, modo 0Eh, 640x200x16)
    { false,    nullptr,  0,  0, 1, drawScanline_PC_Graphics_EGA_640x200_16Colors, VGA_640x240_60Hz,  640, 200 },

    // Emulation::PC_Graphics_EGA_640x350_16Colors (XPC: EGA, modo 10h, 640x350x16 - copia com as linhas pares)
    { false,    nullptr,  0,  0, 2, drawScanline_PC_Graphics_EGA_640x350_16Colors, VGA_640x350_70Hz,  -1,  -1 },

  };

  if (emulation != Emulation::None) {
    auto info = emulationInfo + (int)emulation - 1;

    m_VGADCtrl.setDrawScanlineCallback(info->scanlineCallback, this);
    m_VGADCtrl.setScanlinesPerCallBack(info->scanlinesPerCallback);
    m_VGADCtrl.setResolution(info->modeline, info->viewportWidth, info->viewportHeight);

    if (info->text) {
      setFont(info->font);
      setCursorShape(info->cursorStart, info->cursorEnd);
      m_columns = m_VGADCtrl.getViewPortWidth() / m_font.width;
      m_rows    = m_VGADCtrl.getViewPortHeight() / m_font.height;
    }
  }
}


GraphicsAdapter::GraphicsAdapter()
  : m_VGADCtrl(false),
    m_emulation(Emulation::None),
    m_videoBuffer(nullptr),
    m_rawLUT(nullptr),
    m_cursorRow(0),
    m_cursorCol(0),
    m_cursorStart(0),
    m_cursorEnd(0),
    m_cursorVisible(false),
    m_cursorGlyph(nullptr),
    m_bit7blink(true),
    m_PCGraphicsBackgroundColorIndex(0),
    m_PCGraphicsForegroundColorIndex(15),
    m_PCGraphicsPaletteInUse(0),
    m_monitorMode(MonitorMode::Green),
    m_videoEnabled(true),
    m_egaShadow(nullptr)
{
  // paleta EGA padrao do modo 0Dh: 0..7, 10h..17h
  for (int i = 0; i < 16; ++i)
    m_egaPalette[i] = (i & 7) | ((i & 8) << 1);
  m_font.data = nullptr;
  m_VGADCtrl.begin();
}


GraphicsAdapter::~GraphicsAdapter()
{
  enableVideo(false);
  cleanupFont();
  freeLUT();
  if (m_cursorGlyph)
    heap_caps_free(m_cursorGlyph);
}


bool GraphicsAdapter::enableVideo(bool value)
{
  if (value == m_videoEnabled)
    return m_videoEnabled;

  m_videoEnabled = value;

  if (m_videoEnabled) {
    setupEmulation(m_emulation);
    m_VGADCtrl.run();
  } else {
    m_VGADCtrl.end();
  }

  return !m_videoEnabled;
}


void GraphicsAdapter::setEmulation(Emulation emulation)
{
  if (m_emulation != emulation) {

    bool videoWasEnabled = enableVideo(false);
    freeLUT();

    m_emulation = emulation;
    if (m_emulation != Emulation::None) {
      setupEmulation(emulation);
      setupLUT();
      enableVideo(videoWasEnabled);
    }
  }
}


void GraphicsAdapter::freeLUT()
{
  if (m_rawLUT)
    heap_caps_free(m_rawLUT);
  m_rawLUT = nullptr;
}


void GraphicsAdapter::setupLUT()
{
  switch (m_emulation) {

    case Emulation::None:
      break;

    case Emulation::PC_Text_80x25_16Colors:
    case Emulation::PC_Text_40x25_16Colors:
      // each LUT item contains half pixel (an index to 16 colors palette)
      if (!m_rawLUT)
        m_rawLUT = (uint8_t*) heap_caps_malloc(16, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
      for (int i = 0; i < 16; ++i)
        m_rawLUT[i]  = mapColor(CGAPalette[i]);
      break;

    case Emulation::PC_Graphics_320x200_4Colors:
      // each LUT item contains four pixels (decodes as four raw bytes)
      if (!m_rawLUT)
        m_rawLUT = (uint8_t*) heap_caps_malloc(256 * 4, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
      for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 4; ++j) {
          int pixel = (i >> (6 - j * 2)) & 0b11;
          uint8_t rawPixel = mapColor(pixel == 0 ? CGAPalette[m_PCGraphicsBackgroundColorIndex] : CGAGraphics4ColorsPalette[(m_PCGraphicsPaletteInUse + m_cgaPalShift + 2) % 6][pixel]);
          m_rawLUT[(i * 4) + (j ^ 2)] = rawPixel;
        }
      }
      break;

    case Emulation::PC_Graphics_640x200_2Colors:
      // each LUT item contains eight pixels (decodes as eight raw bytes)
      if (!m_rawLUT)
        m_rawLUT = (uint8_t*) heap_caps_malloc(256 * 8, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
      for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 8; ++j) {
          bool pixel = (i >> (7 - j)) & 1;
          uint8_t rawPixel = mapColor(pixel ? CGAPalette[m_PCGraphicsForegroundColorIndex] : RGB222(0, 0, 0));
          m_rawLUT[(i * 8) + (j ^ 2)] = rawPixel;
        }
      }
      break;

    case Emulation::PC_Graphics_EGA_320x200_16Colors:
    case Emulation::PC_Graphics_EGA_640x200_16Colors:
    case Emulation::PC_Graphics_EGA_640x350_16Colors:
      // XPC: um byte por indice de paleta (0..15)
      if (!m_rawLUT)
        m_rawLUT = (uint8_t*) heap_caps_malloc(16, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
      for (int i = 0; i < 16; ++i)
        setEGAPaletteItem(i, m_egaPalette[i]);
      break;

    case Emulation::PC_Graphics_HGC_720x348:
      // each LUT item contains eight pixels (decodes as eight raw bytes)
      if (!m_rawLUT)
        m_rawLUT = (uint8_t*) heap_caps_malloc(256 * 8, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
      for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 8; ++j) {
          bool pixel = (i >> (7 - j)) & 1;
          uint8_t rawPixel = mapColor(RGB222(pixel * 3, pixel * 3, pixel * 3));
          m_rawLUT[(i * 8) + (j ^ 2)] = rawPixel;
        }
      }
      break;

  }
}


// converte a cor conforme o modo do monitor (colorido, verde, ambar, branco)
uint8_t GraphicsAdapter::mapColor(RGB222 c)
{
  if (m_monitorMode != MonitorMode::Color) {
    // luminancia 0..3 (pesos ~ 0.30 R, 0.59 G, 0.11 B)
    int lum = (c.R * 30 + c.G * 59 + c.B * 11 + 50) / 100;
    if (lum == 0 && (c.R | c.G | c.B))
      lum = 1;   // cores escuras nao somem
    if (lum > 3)
      lum = 3;
    static const RGB222 AMBER[4] = { RGB222(0, 0, 0), RGB222(2, 1, 0), RGB222(3, 2, 0), RGB222(3, 3, 0) };
    switch (m_monitorMode) {
      case MonitorMode::Green: c = RGB222(0, lum, 0);       break;
      case MonitorMode::Amber: c = AMBER[lum];              break;
      case MonitorMode::White: c = RGB222(lum, lum, lum);   break;
      default: break;
    }
  }
  return m_VGADCtrl.createRawPixel(c);
}


// CTRL+ALT+UP: Colorido -> Verde -> Ambar -> Branco -> Colorido ...
void GraphicsAdapter::cycleMonitorMode()
{
  m_monitorMode = (MonitorMode)(((int)m_monitorMode + 1) % 4);
  setupLUT();
}


// XPC: registrador de paleta EGA. No modo 0Dh (monitor de 200 linhas) so valem os bits rgbI:
//      bits 0-2 = cor, bit 4 = intensidade -> mesma cor das 16 do CGA
void GraphicsAdapter::setEGAPaletteItem(int index, uint8_t value)
{
  index &= 15;
  m_egaPalette[index] = value;
  if (m_emulation == Emulation::PC_Graphics_EGA_640x350_16Colors && m_rawLUT) {
    // monitor de 350 linhas: 64 cores (rgbRGB: bits 0-2 = BGR intensos, bits 3-5 = bgr secundarios)
    int r = ((value >> 1) & 2) | ((value >> 5) & 1);
    int g = (value & 2) | ((value >> 4) & 1);
    int b = ((value << 1) & 2) | ((value >> 3) & 1);
    m_rawLUT[index] = mapColor(RGB222(r, g, b));
  } else if ((m_emulation == Emulation::PC_Graphics_EGA_320x200_16Colors || m_emulation == Emulation::PC_Graphics_EGA_640x200_16Colors) && m_rawLUT) {
    int cga = (value & 7) | ((value & 0x10) ? 8 : 0);
    m_rawLUT[index] = mapColor(CGAPalette[cga]);
  }
}


void GraphicsAdapter::setPCGraphicsBackgroundColorIndex(int colorIndex)
{
  m_PCGraphicsBackgroundColorIndex = colorIndex;
  setupLUT();
}


void GraphicsAdapter::setPCGraphicsForegroundColorIndex(int colorIndex)
{
  m_PCGraphicsForegroundColorIndex = colorIndex;
  setupLUT();
}


void GraphicsAdapter::setPCGraphicsPaletteInUse(int paletteIndex)
{
  m_PCGraphicsPaletteInUse = paletteIndex;
  setupLUT();
}


void GraphicsAdapter::setCGAPaletteShift(int value)
{
  m_cgaPalShift = ((value % 6) + 6) % 6;
  if (m_emulation == Emulation::PC_Graphics_320x200_4Colors)
    setupLUT();
}


void GraphicsAdapter::setVideoBuffer(void const * videoBuffer)
{
  m_videoBuffer = (uint8_t*) videoBuffer;
}


void GraphicsAdapter::cleanupFont()
{
  if (m_font.data) {
    heap_caps_free((void*)m_font.data);
    m_font.data = nullptr;
  }
}


void GraphicsAdapter::setFont(FontInfo const * font)
{
  cleanupFont();
  if (font) {
    m_font = *font;
    // copy font data into internal RAM
    auto size = 256 * ((m_font.width + 7) / 8) * m_font.height;
    m_font.data = (uint8_t const *) heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    memcpy((void*)m_font.data, font->data, size);
  }
}


void GraphicsAdapter::setCursorShape(int start, int end)
{
  if (start != m_cursorStart || end != m_cursorEnd) {
    m_cursorStart = start;
    m_cursorEnd   = end;

    // readapt start->end to the actual font height to make sure the cursor is always visible
    if (start <= end && end >= m_font.height) {
      int h = end - start;
      end   = m_font.height - 1;
      start = end - h;
    }

    int charWidthInBytes = (m_font.width + 7) / 8;
    if (!m_cursorGlyph)
      m_cursorGlyph = (uint8_t*) heap_caps_malloc(charWidthInBytes * m_font.height, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    memset(m_cursorGlyph, 0, charWidthInBytes * m_font.height);
    if (end >= start && start >= 0 && start < m_font.height && end < m_font.height)
      memset(m_cursorGlyph + (start * charWidthInBytes), 0xff, (end - start + 1) * charWidthInBytes);
  }
}


void GraphicsAdapter::setCursorPos(int row, int column)
{
  m_cursorRow = row;
  m_cursorCol = column;
}


void IRAM_ATTR GraphicsAdapter::drawScanline_PC_Text_40x25_16Colors(void * arg, uint8_t * dest, int scanLine)
{
  auto ga = (GraphicsAdapter*) arg;

  constexpr int CHARWIDTH        = 8;
  constexpr int CHARHEIGHT       = 8;
  constexpr int CHARWIDTHINBYTES = (CHARWIDTH + 7) / 8;
  constexpr int CHARSIZEINBYTES  = CHARWIDTHINBYTES * CHARHEIGHT;
  constexpr int COLUMNS          = 40;
  constexpr int SCREENWIDTH      = 320;
  constexpr int LINES            = 8;

  if (scanLine == 0)
    ++ga->m_frameCounter;

  int charScanline = scanLine & (CHARHEIGHT - 1);
  int textRow      = scanLine / CHARHEIGHT;

  uint8_t const * fontData = ga->m_font.data + (charScanline * CHARWIDTHINBYTES);

  uint8_t * rawLUT = ga->m_rawLUT;

  uint8_t const * curItem = ga->m_videoBuffer + (textRow * COLUMNS * 2);

  bool showCursor = ga->m_cursorVisible && ga->m_cursorRow == textRow && ((ga->m_frameCounter & 0x1f) < 0xf);
  int cursorCol = ga->m_cursorCol;

  bool bit7blink = ga->m_bit7blink;
  bool blinktime = bit7blink && !((ga->m_frameCounter & 0x3f) < 0x1f);

  for (int textCol = 0; textCol < COLUMNS; ++textCol) {

    int charIdx  = *curItem++;
    int charAttr = *curItem++;

    bool blink = false;
    if (bit7blink) {
      blink = blinktime && (charAttr & 0x80);
      charAttr &= 0x7f;
    }

    uint8_t bg = rawLUT[charAttr >> 4];
    uint8_t fg = blink ? bg : rawLUT[charAttr & 0xf];

    const uint8_t colors[2] = { bg, fg };

    uint8_t const * charBitmapPtr = fontData + charIdx * CHARSIZEINBYTES;

    auto destptr = dest;

    if (showCursor && textCol == cursorCol) {

      uint8_t const * cursorBitmapPtr = ga->m_cursorGlyph + (charScanline * CHARWIDTHINBYTES);

      for (int charRow = 0; charRow < LINES / 2; ++charRow) {

        uint32_t charBitmap = *charBitmapPtr | *cursorBitmapPtr;

        *(destptr + 0) = colors[(bool)(charBitmap & 0x20)];
        *(destptr + 1) = colors[(bool)(charBitmap & 0x10)];
        *(destptr + 2) = colors[(bool)(charBitmap & 0x80)];
        *(destptr + 3) = colors[(bool)(charBitmap & 0x40)];
        *(destptr + 4) = colors[(bool)(charBitmap & 0x02)];
        *(destptr + 5) = colors[(bool)(charBitmap & 0x01)];
        *(destptr + 6) = colors[(bool)(charBitmap & 0x08)];
        *(destptr + 7) = colors[(bool)(charBitmap & 0x04)];

        destptr += SCREENWIDTH;
        charBitmapPtr += CHARWIDTHINBYTES;
        cursorBitmapPtr += CHARWIDTHINBYTES;
      }

    } else {

      for (int charRow = 0; charRow < LINES / 2; ++charRow) {

        uint32_t charBitmap = *charBitmapPtr;

        *(destptr + 0) = colors[(bool)(charBitmap & 0x20)];
        *(destptr + 1) = colors[(bool)(charBitmap & 0x10)];
        *(destptr + 2) = colors[(bool)(charBitmap & 0x80)];
        *(destptr + 3) = colors[(bool)(charBitmap & 0x40)];
        *(destptr + 4) = colors[(bool)(charBitmap & 0x02)];
        *(destptr + 5) = colors[(bool)(charBitmap & 0x01)];
        *(destptr + 6) = colors[(bool)(charBitmap & 0x08)];
        *(destptr + 7) = colors[(bool)(charBitmap & 0x04)];

        destptr += SCREENWIDTH;
        charBitmapPtr += CHARWIDTHINBYTES;
      }

    }

    dest += 8;

  }
}


void IRAM_ATTR GraphicsAdapter::drawScanline_PC_Text_80x25_16Colors(void * arg, uint8_t * dest, int scanLine)
{
  auto ga = (GraphicsAdapter*) arg;

  constexpr int CHARWIDTH        = 8;
  constexpr int CHARHEIGHT       = 16;
  constexpr int CHARWIDTHINBYTES = (CHARWIDTH + 7) / 8;
  constexpr int CHARSIZEINBYTES  = CHARWIDTHINBYTES * CHARHEIGHT;
  constexpr int COLUMNS          = 80;
  constexpr int SCREENWIDTH      = 640;
  constexpr int LINES            = 16;

  if (scanLine == 0)
    ++ga->m_frameCounter;

  int charScanline = scanLine & (CHARHEIGHT - 1);
  int textRow      = scanLine / CHARHEIGHT;

  uint8_t const * fontData = ga->m_font.data + (charScanline * CHARWIDTHINBYTES);

  uint8_t * rawLUT = ga->m_rawLUT;

  uint8_t const * curItem = ga->m_videoBuffer + (textRow * COLUMNS * 2);

  bool showCursor = ga->m_cursorVisible && ga->m_cursorRow == textRow && ((ga->m_frameCounter & 0x1f) < 0xf);
  int cursorCol = ga->m_cursorCol;

  bool bit7blink = ga->m_bit7blink;
  bool blinktime = bit7blink && !((ga->m_frameCounter & 0x3f) < 0x1f);

  for (int textCol = 0; textCol < COLUMNS; ++textCol) {

    int charIdx  = *curItem++;
    int charAttr = *curItem++;

    bool blink = false;
    if (bit7blink) {
      blink = blinktime && (charAttr & 0x80);
      charAttr &= 0x7f;
    }

    uint8_t bg = rawLUT[charAttr >> 4];
    uint8_t fg = blink ? bg : rawLUT[charAttr & 0xf];

    const uint8_t colors[2] = { bg, fg };

    uint8_t const * charBitmapPtr = fontData + charIdx * CHARSIZEINBYTES;

    auto destptr = dest;

    if (showCursor && textCol == cursorCol) {

      uint8_t const * cursorBitmapPtr = ga->m_cursorGlyph + (charScanline * CHARWIDTHINBYTES);

      for (int charRow = 0; charRow < LINES / 2; ++charRow) {

        uint32_t charBitmap = *charBitmapPtr | *cursorBitmapPtr;

        *(destptr + 0) = colors[(bool)(charBitmap & 0x20)];
        *(destptr + 1) = colors[(bool)(charBitmap & 0x10)];
        *(destptr + 2) = colors[(bool)(charBitmap & 0x80)];
        *(destptr + 3) = colors[(bool)(charBitmap & 0x40)];
        *(destptr + 4) = colors[(bool)(charBitmap & 0x02)];
        *(destptr + 5) = colors[(bool)(charBitmap & 0x01)];
        *(destptr + 6) = colors[(bool)(charBitmap & 0x08)];
        *(destptr + 7) = colors[(bool)(charBitmap & 0x04)];

        destptr += SCREENWIDTH;
        charBitmapPtr += CHARWIDTHINBYTES;
        cursorBitmapPtr += CHARWIDTHINBYTES;
      }

    } else {

      for (int charRow = 0; charRow < LINES / 2; ++charRow) {

        uint32_t charBitmap = *charBitmapPtr;

        *(destptr + 0) = colors[(bool)(charBitmap & 0x20)];
        *(destptr + 1) = colors[(bool)(charBitmap & 0x10)];
        *(destptr + 2) = colors[(bool)(charBitmap & 0x80)];
        *(destptr + 3) = colors[(bool)(charBitmap & 0x40)];
        *(destptr + 4) = colors[(bool)(charBitmap & 0x02)];
        *(destptr + 5) = colors[(bool)(charBitmap & 0x01)];
        *(destptr + 6) = colors[(bool)(charBitmap & 0x08)];
        *(destptr + 7) = colors[(bool)(charBitmap & 0x04)];

        destptr += SCREENWIDTH;
        charBitmapPtr += CHARWIDTHINBYTES;
      }

    }

    dest += 8;

  }
}


// offset 0x0000 for even scan lines (bit 0 = 0), lines 0, 2, 4...
// offset 0x2000 for odd scan lines  (bit 0 = 1), lines 1, 3, 5...
void IRAM_ATTR GraphicsAdapter::drawScanline_PC_Graphics_320x200_4Colors(void * arg, uint8_t * dest, int scanLine)
{
  constexpr int WIDTH         = 320;
  constexpr int PIXELSPERBYTE = 4;
  constexpr int WIDTHINBYTES  = WIDTH / PIXELSPERBYTE;

  auto ga = (GraphicsAdapter*) arg;

  auto src = ga->m_videoBuffer + ((scanLine & 1) << 13) + WIDTHINBYTES * (scanLine >> 1);

  auto dest32 = (uint32_t*) dest;

  auto LUT32 = (uint32_t*) ga->m_rawLUT;

  for (int col = 0; col < WIDTH; col += PIXELSPERBYTE) {
    *dest32++ = LUT32[*src++];
  }
}


// offset 0x0000 for even scan lines (bit 0 = 0), lines 0, 2, 4...
// offset 0x2000 for odd scan lines  (bit 0 = 1), lines 1, 3, 5...
void IRAM_ATTR GraphicsAdapter::drawScanline_PC_Graphics_640x200_2Colors(void * arg, uint8_t * dest, int scanLine)
{
  constexpr int WIDTH         = 640;
  constexpr int PIXELSPERBYTE = 8;
  constexpr int WIDTHINBYTES  = WIDTH / PIXELSPERBYTE;

  auto ga = (GraphicsAdapter*) arg;

  auto src = ga->m_videoBuffer + ((scanLine & 1) << 13) + WIDTHINBYTES * (scanLine >> 1);

  auto dest64 = (uint64_t*) dest;

  auto LUT64 = (uint64_t*) ga->m_rawLUT;

  for (int col = 0; col < WIDTH; col += PIXELSPERBYTE) {
    *dest64++ = LUT64[*src++];
  }
}


// offset 0x0000 for lines 0, 4, 8, 12...
// offset 0x2000 for lines 1, 5, 9, 13...
// offset 0x4000 for lines 2, 6, 10, 14...
// offset 0x6000 for lines 3, 7, 11, 15...
void IRAM_ATTR GraphicsAdapter::drawScanline_PC_Graphics_HGC_720x348(void * arg, uint8_t * dest, int scanLine)
{
  constexpr int WIDTH         = 720;
  constexpr int PIXELSPERBYTE = 8;
  constexpr int WIDTHINBYTES  = WIDTH / PIXELSPERBYTE;

  auto ga = (GraphicsAdapter*) arg;

  auto src = ga->m_videoBuffer + ((scanLine & 0b11) << 13) + WIDTHINBYTES * (scanLine >> 2);

  auto dest64 = (uint64_t*) dest;

  auto LUT64 = (uint64_t*) ga->m_rawLUT;

  for (int col = 0; col < WIDTH; col += PIXELSPERBYTE)
    *dest64++ = LUT64[*src++];

  ++scanLine;
  src = ga->m_videoBuffer + ((scanLine & 0b11) << 13) + WIDTHINBYTES * (scanLine >> 2);

  for (int col = 0; col < WIDTH; col += PIXELSPERBYTE)
    *dest64++ = LUT64[*src++];
}









// XPC: EGA 320x200x16 (modo 0Dh)
// 40 bytes por linha em cada um dos 4 planos; cada bit e um pixel (bit 7 = pixel mais a esquerda)
// A tabela espalha os 8 bits de um byte em 8 nibbles (um por pixel); juntando os 4 planos
// deslocados de 0..3 bits obtem-se os 8 indices de cor de uma vez.
static const DRAM_ATTR uint32_t EGA_SPREAD[256] = {
#define S1(b) ((((b) >> 7) & 1) << 0 | (((b) >> 6) & 1) << 4 | (((b) >> 5) & 1) << 8 | (((b) >> 4) & 1) << 12 | \
               (((b) >> 3) & 1) << 16 | (((b) >> 2) & 1) << 20 | (((b) >> 1) & 1) << 24 | (((b) >> 0) & 1) << 28)
#define S4(b) S1(b), S1(b + 1), S1(b + 2), S1(b + 3)
#define S16(b) S4(b), S4(b + 4), S4(b + 8), S4(b + 12)
#define S64(b) S16(b), S16(b + 16), S16(b + 32), S16(b + 48)
  S64(0), S64(64), S64(128), S64(192)
#undef S64
#undef S16
#undef S4
#undef S1
};


void IRAM_ATTR GraphicsAdapter::drawScanline_PC_Graphics_EGA_320x200_16Colors(void * arg, uint8_t * dest, int scanLine)
{
  constexpr int WIDTHINBYTES = 320 / 8;

  auto ga  = (GraphicsAdapter*) arg;
  auto lut = ga->m_rawLUT;
  auto src = ga->m_egaShadow + scanLine * ga->m_egaPitch;

  for (int col = 0; col < WIDTHINBYTES; ++col, dest += 8) {
    uint32_t v = *src++;
    uint32_t pix = EGA_SPREAD[v & 0xff] | (EGA_SPREAD[(v >> 8) & 0xff] << 1) | (EGA_SPREAD[(v >> 16) & 0xff] << 2) | (EGA_SPREAD[v >> 24] << 3);
    // ordem dos bytes no buffer de linha do VGADirectController: indices ^ 2
    dest[2] = lut[pix & 15];
    dest[3] = lut[(pix >> 4) & 15];
    dest[0] = lut[(pix >> 8) & 15];
    dest[1] = lut[(pix >> 12) & 15];
    dest[6] = lut[(pix >> 16) & 15];
    dest[7] = lut[(pix >> 20) & 15];
    dest[4] = lut[(pix >> 24) & 15];
    dest[5] = lut[(pix >> 28) & 15];
  }
}


void IRAM_ATTR GraphicsAdapter::drawScanline_PC_Graphics_EGA_640x200_16Colors(void * arg, uint8_t * dest, int scanLine)
{
  constexpr int WIDTHINBYTES = 640 / 8;

  auto ga  = (GraphicsAdapter*) arg;
  auto lut = ga->m_rawLUT;
  auto src = ga->m_egaShadow + scanLine * ga->m_egaPitch;

  for (int col = 0; col < WIDTHINBYTES; ++col, dest += 8) {
    uint32_t v = *src++;
    uint32_t pix = EGA_SPREAD[v & 0xff] | (EGA_SPREAD[(v >> 8) & 0xff] << 1) | (EGA_SPREAD[(v >> 16) & 0xff] << 2) | (EGA_SPREAD[v >> 24] << 3);
    dest[2] = lut[pix & 15];
    dest[3] = lut[(pix >> 4) & 15];
    dest[0] = lut[(pix >> 8) & 15];
    dest[1] = lut[(pix >> 12) & 15];
    dest[6] = lut[(pix >> 16) & 15];
    dest[7] = lut[(pix >> 20) & 15];
    dest[4] = lut[(pix >> 24) & 15];
    dest[5] = lut[(pix >> 28) & 15];
  }
}


// modo 10h (640x350): a copia tem so as linhas pares (175 linhas); chamada a cada 2 linhas, a segunda repete a primeira
void IRAM_ATTR GraphicsAdapter::drawScanline_PC_Graphics_EGA_640x350_16Colors(void * arg, uint8_t * dest, int scanLine)
{
  constexpr int WIDTHINBYTES = 640 / 8;

  auto ga   = (GraphicsAdapter*) arg;
  auto lut  = ga->m_rawLUT;
  auto src  = ga->m_egaShadow + (scanLine >> 1) * ga->m_egaPitch;
  auto line = dest;

  for (int col = 0; col < WIDTHINBYTES; ++col, dest += 8) {
    uint32_t v = *src++;
    uint32_t pix = EGA_SPREAD[v & 0xff] | (EGA_SPREAD[(v >> 8) & 0xff] << 1) | (EGA_SPREAD[(v >> 16) & 0xff] << 2) | (EGA_SPREAD[v >> 24] << 3);
    dest[2] = lut[pix & 15];
    dest[3] = lut[(pix >> 4) & 15];
    dest[0] = lut[(pix >> 8) & 15];
    dest[1] = lut[(pix >> 12) & 15];
    dest[6] = lut[(pix >> 16) & 15];
    dest[7] = lut[(pix >> 20) & 15];
    dest[4] = lut[(pix >> 24) & 15];
    dest[5] = lut[(pix >> 28) & 15];
  }
  memcpy(line + 640, line, 640);
}


}  // xpc namespace
