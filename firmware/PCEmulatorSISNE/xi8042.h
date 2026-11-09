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

#include "fabgl.h"



namespace xpc {

using namespace fabgl;



// 8042 PS/2 Keyboard Controller. Actually it is emulated how it is seen in the IBM AT

class i8042 {

public:
  // XPC: ha byte do teclado/mouse esperando o programa ler (porta 60h)
  bool outputFull() { return m_STATUS & 0x01; }

  typedef bool (*InterruptCallback)(void * context);

  i8042();
  ~i8042();

  void init();

  void reset();

  void setCallbacks(void * context, InterruptCallback keyboardInterrupt, InterruptCallback mouseInterrupt, InterruptCallback reset, InterruptCallback sysReq) {
    m_context           = context;
    m_keyboardInterrupt = keyboardInterrupt;
    m_mouseInterrupt    = mouseInterrupt;
    m_reset             = reset;
    m_sysReq            = sysReq;
  }

  // teclas de atalho (consumidas aqui, o programa emulado nao as recebe)
  //   CTRL+ALT+UP   : menu (chamado depois que CTRL e ALT forem soltos)
  //   CTRL+ALT+DOWN : ciclo do monitor (colorido/verde/ambar/branco)
  void setHotkeyCallbacks(InterruptCallback menu, InterruptCallback cycle) { m_hotkeyMenu = menu; m_hotkeyCycle = cycle; }
  // CTRL+ALT+RIGHT (proxima paleta CGA) e CTRL+ALT+LEFT (paleta original)
  void setPaletteHotkeys(InterruptCallback next, InterruptCallback reset) { m_hotkeyPalNext = next; m_hotkeyPalReset = reset; }

  void tick();

  uint8_t read(int address);
  void write(int address, uint8_t value);

  Keyboard * keyboard()  { return m_keyboard; }
  Mouse * mouse()        { return m_mouse; }

  void enableMouse(bool value);

private:

  void execCommand();
  void updateCommandByte(uint8_t newValue);
  bool trigKeyboardInterrupt();
  bool trigMouseInterrupt();
  void checkSysReq(int scode2);
  bool scancodeReady();
  int  nextScancode();

  PS2Controller     m_PS2Controller;
  Keyboard *        m_keyboard;
  Mouse *           m_mouse;

  void *            m_context;
  InterruptCallback m_keyboardInterrupt;
  InterruptCallback m_mouseInterrupt;
  InterruptCallback m_reset;
  InterruptCallback m_sysReq;

  uint8_t           m_STATUS;
  uint8_t           m_DBBOUT;
  uint8_t           m_DBBIN;
  uint8_t           m_commandByte;
  bool              m_writeToMouse; // if True next byte on port 0 (0x60) is transferred to mouse port
  MousePacket       m_mousePacket;
  int               m_mousePacketIdx;

  // used when a command requires a parameter
  uint8_t           m_executingCommand; // 0 = none

  SemaphoreHandle_t m_mutex;

  int               m_mouseIntTrigs;
  int               m_keybIntTrigs;

  bool              m_sysReqTriggered;

  InterruptCallback m_hotkeyMenu  = nullptr;
  InterruptCallback m_hotkeyCycle = nullptr;
  InterruptCallback m_hotkeyPalNext = nullptr;
  InterruptCallback m_hotkeyPalReset = nullptr;
  bool              m_menuPending = false;
  bool              m_resetPending = false;
public:
  uint32_t          m_diagPackets = 0, m_diagIrqs = 0;   // diagnostico do mouse (menu)
  bool              diagMouseOk() { return m_mouse && m_mouse->isMouseAvailable(); }
  uint8_t           diagCmdByte() { return m_commandByte; }
private:
  bool              m_rawCtrl = false, m_rawAlt = false, m_rawBreak = false, m_rawE0 = false;
  bool              rawTrack(int c);
  void              hotkeysOnly();
  int               m_obfAge = 0;
  uint8_t           m_mouseFifo[32];
  int               m_mouseFifoHead = 0, m_mouseFifoCount = 0;
  static constexpr int HOLDSIZE = 64;       // teclas guardadas enquanto o teclado estava bloqueado
  uint8_t           m_hold[HOLDSIZE];
  uint32_t          m_holdHead = 0, m_holdTail = 0;
  int               m_next   = -1;
  int               m_stash  = -1;
  bool              m_heldE0 = false;
  bool              m_hkCtrl = false;
  bool              m_hkAlt  = false;
  bool              m_hkBreak = false;

};











}  // xpc namespace
