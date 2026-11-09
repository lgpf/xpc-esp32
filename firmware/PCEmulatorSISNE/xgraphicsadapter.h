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


#pragma once


/**
 * @file
 *
 * @brief This file contains fabgl::GraphicsAdapter definition.
 */



#include <stdlib.h>
#include <stdint.h>

#include "fabgl.h"



namespace xpc {

using namespace fabgl;

// modos de cor do monitor
enum class MonitorMode { Color, Green, Amber, White };




class GraphicsAdapter {

public:

  enum class Emulation {
    None,
    
    // CGA Color Text Mode (PC BIOS 00h/01h)
    // Requires 2 * 40 * 25 = 2000 bytes
    PC_Text_40x25_16Colors,
    
    // CGA Color Text Mode (PC BIOS 02h/03h)
    // This is not visually like CGA because use 640x400 screen with 8x16 fonts, while should be 640x200 with 8x8 fonts
    // Requires 2 * 80 * 25 = 4000 bytes
    PC_Text_80x25_16Colors,
    
    // CGA 320x200, 4 Colors Graphics Mode (PC BIOS 04h/05h)
    // Requires 320 / 4 * 200 = 16000 bytes
    PC_Graphics_320x200_4Colors,
    
    // CGA 640x200, 2 Colors Graphics Mode (PC BIOS 06h)
    // Requires 640 / 8 * 200 = 16000 bytes
    PC_Graphics_640x200_2Colors,
    
    // Hercules 720x348 Graphics Black/White
    // Requires 720 / 8 * 348 = 31320 bytes
    PC_Graphics_HGC_720x348,

    // XPC: EGA 320x200, 16 cores (modo 0Dh), 4 planos de bits
    PC_Graphics_EGA_320x200_16Colors,

    // XPC: EGA 640x200, 16 cores (modo 0Eh), 4 planos de bits
    PC_Graphics_EGA_640x200_16Colors,
    PC_Graphics_EGA_640x350_16Colors,
  };


  GraphicsAdapter();
  ~GraphicsAdapter();

  void setEmulation(Emulation emulation);
  Emulation emulation()                                    { return m_emulation; }

  // XPC: leitura do estado (para o menu em texto salvar e restaurar a tela)
  uint8_t const * videoBuffer()                            { return m_videoBuffer; }
  bool bit7Blink()                                         { return m_bit7blink; }
  bool cursorVisible()                                     { return m_cursorVisible; }
  bool isVideoEnabled()                                    { return m_videoEnabled; }

  bool enableVideo(bool value);

  void setVideoBuffer(void const * videoBuffer);

  // text modes methods
  void setCursorShape(int start, int end);
  void setCursorPos(int row, int column);
  void setCursorVisible(bool value)                        { m_cursorVisible = value; }
  void setBit7Blink(bool value)                            { m_bit7blink = value; }
  int getTextColumns()                                     { return m_columns; }
  int getTextRows()                                        { return m_rows; }

  // PC graphics modes methods
  void setMonitorMode(MonitorMode value)  { m_monitorMode = value; setupLUT(); }
  MonitorMode monitorMode()                { return m_monitorMode; }
  void cycleMonitorMode();

  void setPCGraphicsBackgroundColorIndex(int colorIndex);
  void setPCGraphicsForegroundColorIndex(int colorIndex);
  void setPCGraphicsPaletteInUse(int paletteIndex);
  // XPC: deslocamento da paleta CGA 320x200 (0 = a do programa; 1..5 = as outras paletas do CGA)
  void setCGAPaletteShift(int value);
  int  CGAPaletteShift() { return m_cgaPalShift; }
  int getGraphWidth()                                      { return m_VGADCtrl.getViewPortWidth(); }
  int getGraphHeight()                                     { return m_VGADCtrl.getViewPortHeight(); }

  bool VSync()                                             { return m_VGADCtrl.VSync(); }

  // XPC: EGA (modo 0Dh)
  void setEGAShadow(uint32_t const * shadow)               { m_egaShadow = shadow; }
  void setEGAPitch(int value)                              { m_egaPitch = value; }   // posicoes por linha   // 8K posicoes, byte p = plano p
  void setEGAPaletteItem(int index, uint8_t value);


private:

  void cleanupFont();

  void freeLUT();
  void setupLUT();

  void setFont(FontInfo const * font);

  void createCursorGlyph(int width, int height, int start, int end);

  void setupEmulation(Emulation emulation);

  static void drawScanline_PC_Text_80x25_16Colors(void * arg, uint8_t * dest, int scanLine);
  static void drawScanline_PC_Text_40x25_16Colors(void * arg, uint8_t * dest, int scanLine);
  static void drawScanline_PC_Graphics_320x200_4Colors(void * arg, uint8_t * dest, int scanLine);
  static void drawScanline_PC_Graphics_640x200_2Colors(void * arg, uint8_t * dest, int scanLine);
  static void drawScanline_PC_Graphics_HGC_720x348(void * arg, uint8_t * dest, int scanLine);
  static void drawScanline_PC_Graphics_EGA_320x200_16Colors(void * arg, uint8_t * dest, int scanLine);
  static void drawScanline_PC_Graphics_EGA_640x200_16Colors(void * arg, uint8_t * dest, int scanLine);
  static void drawScanline_PC_Graphics_EGA_640x350_16Colors(void * arg, uint8_t * dest, int scanLine);


  VGADirectController m_VGADCtrl;
  Emulation           m_emulation;
  uint8_t const *     m_videoBuffer;

  uint8_t *           m_rawLUT;

  MonitorMode         m_monitorMode;
  uint8_t             mapColor(RGB222 c);

  uint32_t            m_frameCounter;

  // text mode parameters
  FontInfo            m_font;
  int16_t             m_columns;
  int16_t             m_rows;
  int16_t             m_cursorRow;
  int16_t             m_cursorCol;
  uint8_t             m_cursorStart;  // cursor shape scanline start
  uint8_t             m_cursorEnd;    // cursor shape scanline end
  bool                m_cursorVisible;
  uint8_t *           m_cursorGlyph;
  bool                m_bit7blink;

  // PC graphics parameters
  int8_t              m_PCGraphicsBackgroundColorIndex; // used as background color index on 320x200
  int8_t              m_PCGraphicsForegroundColorIndex; // used as foreground color index on 640x200
  int8_t              m_cgaPalShift = 0;
  int8_t              m_PCGraphicsPaletteInUse;         // 0 = palette 0 low intensity, 1 = palette 0 high intensity, 2 = palette 1 low intensity, 3 = palette 2 high intensity

  bool                m_videoEnabled;

  // XPC: EGA
  uint32_t const *    m_egaShadow;        // pagina visivel: 8K posicoes, byte p = plano p
  int                 m_egaPitch = 40;    // posicoes por linha na pagina visivel
  uint8_t             m_egaPalette[16];   // registradores de paleta do controlador de atributos

};



};  // xpc namespace
