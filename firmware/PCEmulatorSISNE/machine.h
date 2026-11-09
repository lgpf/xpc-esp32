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
#include "xgraphicsadapter.h"
#include "emudevs/i8086.h"
#include "emudevs/PIC8259.h"
#include "emudevs/PIT8253.h"
#include "xi8042.h"
#include "emudevs/MC146818.h"
#include "emudevs/PC8250.h"
#include "devdrivers/MCP23S17.h"

#include "bios.h"
#include "adlib.h"


#define RAM_SIZE             1048576    // must correspond to bios MEMSIZE
#define VIDEOMEMSIZE         65536

// XPC: EGA - 4 planos de 64KB (256KB, na PSRAM) e copia da pagina visivel (4 x 8KB, RAM interna)
#define EGA_PLANESIZE        65536
#define EGA_PLANEMASK        (EGA_PLANESIZE - 1)
#define EGA_SHADOWSIZE       8192



// maximum number of serial ports
#define SERIALPORTS          2



using xpc::GraphicsAdapter;
using fabgl::PIC8259;
using fabgl::PIT8253;
using xpc::i8042;
using fabgl::MC146818;
using fabgl::MCP23S17;
using fabgl::SerialPort;
using fabgl::PC8250;


#ifdef FABGL_EMULATED
typedef void (*StepCallback)(void *);
#endif


typedef void (*SysReqCallback)();




// XPC: speaker por amostras (modo PCM). Cada mudanca do bit 1 da porta 61h vira um evento com o horario (us);
//      o gerador toca os eventos com um atraso fixo e, a cada amostra, usa a fracao de tempo em que o
//      alto-falante ficou ligado (como DOSBox/PCem). Serve para som digitalizado (Battle Chess) e para notas
//      feitas com pulsos (Digger, California Games). Pulsos isolados e curtos contam como um pulso minimo
//      (no PC real o cone responde ao "tranco" e o som e audivel).
class SpeakerPCMGenerator : public WaveformGenerator {
public:
  static constexpr int RING = 1024;   // ~50ms de som digitalizado (memoria interna e curta)
  // eventos em 16 bits: horario em us (modulo 65536) com o nivel no bit 0. 2KB de RAM interna,
  // alocados so quando o modo e usado (sem memoria: fica no Classic, sem travar)
  bool begin() {
    if (!m_ev)
      m_ev = (uint16_t *) heap_caps_malloc(RING * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return m_ev != nullptr;
  }
  void push(bool level) {
    if (!m_ev)
      return;
    int h = m_head;
    m_ev[h] = ((uint16_t) esp_timer_get_time() & 0xfffe) | (level ? 1 : 0);
    m_head = (h + 1) & (RING - 1);
  }
  void setMute(bool v) { m_mute = v; }
  void setFrequency(int value) { }
  int IRAM_ATTR getSample();
private:
  uint16_t * volatile m_ev = nullptr;
  volatile bool    m_mute = false;
  volatile int     m_head = 0;
  int              m_tail = 0;
  int64_t          m_play = 0;       // horario da amostra atual (1/256 us)
  bool             m_level = false;
  int64_t          m_riseT = 0;
  int64_t          m_lastRise = 0;
  int64_t          m_credit = 0;     // tempo "ligado" ainda nao entregue (1/256 us)
  int32_t          m_xPrev = 0, m_yPrev = 0;
};

class Machine {

public:

  void setTimerSync(bool v)  { m_timerSync = v; if (!v) m_lostIRQ0 = 0; }
  bool timerSync()           { return m_timerSync; }

  Machine();
  ~Machine();

  void setBaseDirectory(char const * value)    { m_baseDir = value; }

  void setDriveImage(int drive, char const * filename, int cylinders = 0, int heads = 0, int sectors = 0);
  
  bool diskChanged(int drive)                  { return m_diskChanged[drive]; }
  void resetDiskChanged(int drive)             { m_diskChanged[drive] = false; }

  void setBootDrive(int drive)                 { m_bootDrive = drive; }

  // configuracoes do XPC (definidas antes de run())
  //   placa de video: 0 = CGA, 1 = Hercules (com CGA), 2 = EGA (modo 0Dh)
  //   som: 0 = speaker, 1 = speaker + AdLib
  //   teclado: 0 = US, 1 = ABNT2
  void setVideoCard(int value)                 { m_videoCard = value; }
  int videoCard()                              { return m_videoCard; }
  void setAdLib(bool value)                    { m_adlibOn = value; }
  bool adlib()                                 { return m_adlibOn; }
  void setKbdLayout(int value)                 { m_kbdLayout = value; }
  int kbdLayout()                              { return m_kbdLayout; }

  void pauseSound(bool value);

  // XPC: EGA (placa de video 2)
  bool egaPresent()                            { return m_egaVRAM != nullptr; }
  bool egaActive()                             { return m_egaActive; }
  bool setEGAMode(int mode, bool clear);
  int  egaMode()                               { return m_egaMode; }
  void egaLeave()                              { m_egaActive = false; m_egaDirect = false; m_egaHalf = false; m_egaCompact = false; m_egaShadowSize = 0; }   // sem copias fora do modo EGA
  void egaDrawChar(int page, int row, int col, uint8_t ch, uint8_t color);
  void egaScroll(int page, bool up, int lines, int r1, int c1, int r2, int c2, uint8_t color);
  void egaTTY(int page, uint8_t ch, uint8_t color);
  void egaWriteChars(int page, uint8_t ch, uint8_t color, int count);
  void egaSetPaletteReg(int index, uint8_t value);
  void egaPutPixel(int page, int x, int y, uint8_t color);
  uint8_t egaGetPixel(int page, int x, int y);

  void setSysReqCallback(SysReqCallback value) { m_sysReqCallback = value; }
  
  void setCOM1(SerialPort * serialPort);
  
  void setCOM2(SerialPort * serialPort);

  void run();

  void trigReset()                     { m_reset = true; }

  // XPC: memoria do PC em KB (256, 512, 640 ou 704; 704 so sem EGA). Vale no proximo reset.
  void setRamKB(int kb)                { m_ramKB = kb; }
  // XPC: idioma da mensagem "sem disco de boot" da BIOS (true = portugues). Vale ja e a cada reset
  void setBootMsgPT(bool value);
  // XPC: bipe do POST (900 Hz, 150 ms) assim que a maquina iniciar
  void requestPostBeep()               { m_postBeep = true; }
  int ramKB()                          { return (m_ramKB > 640 && m_videoCard >= 2) ? 640 : m_ramKB; }

  uint32_t ticksCounter()              { return m_ticksCounter; }

  i8042 * getI8042()                   { return &m_i8042; }

  MC146818 * getMC146818()             { return &m_MC146818; }

  uint8_t * memory()                   { return s_memory; }

  uint8_t * videoMemory()              { return s_videoMemory; }

  uint8_t * frameBuffer()              { return m_frameBuffer; }

  GraphicsAdapter * graphicsAdapter()  { return &m_graphicsAdapter; }

  FILE * disk(int index)               { return m_disk[index]; }
  char const * diskFilename(int index) { return m_diskFilename[index]; }
  uint64_t diskSize(int index)         { return m_diskSize[index]; }
  uint16_t diskCylinders(int index)    { return m_diskCylinders[index]; }
  uint8_t diskHeads(int index)         { return m_diskHeads[index]; }
  uint8_t diskSectors(int index)       { return m_diskSectors[index]; }

  void dumpMemory(char const * filename);
  void dumpInfo(char const * filename);

  #ifdef FABGL_EMULATED
  void setStepCallback(StepCallback value)  { m_stepCallback = value; }
  #endif

private:


  static void runTask(void * pvParameters);

  void init();
  void reset();

  void tick();

  void setCGAMode();
  void setCGA6845Register(uint8_t value);

  void setHGCMode();
  bool HGCGraphicsActive();
  void setHGC6845Register(uint8_t value);

  static void writePort(void * context, int address, uint8_t value);
  static uint8_t readPort(void * context, int address);

  static void writeVideoMemory8(void * context, int address, uint8_t value);
  static void writeVideoMemory16(void * context, int address, uint16_t value);
  static uint8_t readVideoMemory8(void * context, int address);
  static uint16_t readVideoMemory16(void * context, int address);

  static bool interrupt(void * context, int num);

  static void PITChangeOut(void * context, int timerIndex);

  static bool MC146818Interrupt(void * context);
  
  static bool COM1Interrupt(PC8250 * source, void * context);
  static bool COM2Interrupt(PC8250 * source, void * context);

  static bool keyboardInterrupt(void * context);
  static bool mouseInterrupt(void * context);
  static bool resetMachine(void * context);
  static bool sysReq(void * context);
  static bool monitorModeHotkey(void * context);
  static bool paletteNextHotkey(void * context);
  static bool paletteResetHotkey(void * context);
  static bool menuHotkey(void * context);

  void speakerSetFreq();
  void speakerEnableDisable();

  void autoDetectDriveGeometry(int drive);


  #ifdef FABGL_EMULATED
  StepCallback             m_stepCallback;
  #endif

  bool                     m_reset;

  GraphicsAdapter          m_graphicsAdapter;

  BIOS                     m_BIOS;

  // 0, 1 = floppy
  // >= 2 = hard disk
  char *                   m_diskFilename[DISKCOUNT];
  bool                     m_diskChanged[DISKCOUNT];
  FILE *                   m_disk[DISKCOUNT];
  uint64_t                 m_diskSize[DISKCOUNT];
  uint16_t                 m_diskCylinders[DISKCOUNT];
  uint8_t                  m_diskHeads[DISKCOUNT];
  uint8_t                  m_diskSectors[DISKCOUNT];

  static uint8_t *         s_memory;
  static uint8_t *         s_videoMemory;
  uint8_t *                m_frameBuffer;

  // 8259 Programmable Interrupt Controllers
  PIC8259                  m_PIC8259A;  // master
  PIC8259                  m_PIC8259B;  // slave

  // 8253 Programmable Interval Timers
  // pin connections of PIT8253 on the IBM XT:
  //    gate-0 = gate-1 = +5V
  //    gate-2 = TIM2GATESPK
  //    clk-0  = clk-1  = clk-2 = 1193182 Hz
  //    out-0  = IRQ0
  //    out-1  = RAM refresh
  //    out-2  = speaker
  PIT8253                  m_PIT8253;

  // 8042 PS/2 Keyboard Controller
  i8042                    m_i8042;

  TaskHandle_t             m_taskHandle;

  uint32_t                 m_ticksCounter;

  // CGA
  uint8_t                  m_CGA6845SelectRegister;
  uint8_t                  m_CGA6845[256];
  uint16_t                 m_CGAMemoryOffset;
  uint8_t                  m_CGAModeReg;
  uint8_t                  m_CGAColorReg;
  uint16_t                 m_CGAVSyncQuery;

  // Hercules
  uint8_t                  m_HGC6845SelectRegister;
  uint8_t                  m_HGC6845[18];
  uint16_t                 m_HGCMemoryOffset;
  uint8_t                  m_HGCModeReg;
  uint8_t                  m_HGCSwitchReg;
  uint16_t                 m_HGCVSyncQuery;

  // speaker/audio
  bool                     m_speakerDataEnable;
  SoundGenerator           m_soundGen;
  SineWaveformGenerator    m_sinWaveGen;
  // XPC: speaker "direto" (bit 1 da porta 61h com o timer 2 parado, ex.: Digger). O gerador de som do
  //      ESP32 calcula as amostras em blocos, entao nao da para seguir cada pulso: mede-se o intervalo
  //      entre pulsos e toca-se uma onda quadrada estreita nessa frequencia.
  SquareWaveformGenerator  m_bitbangGen;
  SpeakerPCMGenerator      m_pcmGen;
  int                      m_spkMode = 0;         // Setup > PC speaker: 0 Auto (padrao), 1 PCM, 2 Classic
  bool                     m_pcmActive = false;   // Auto: som digitalizado detectado (pulsos rapidos)
  int64_t                  m_rateStart = 0;
  int                      m_rateRises = 0;
public:
  void setSpeakerMode(int v);
  int  speakerMode() { return m_spkMode; }
private:
  int64_t                  m_bitbangLastRise = 0;
  int                      m_bitbangFreq     = 0;
  bool                     m_bitbangLevel    = false;
public:
  uint32_t                 m_bbPulses = 0;    // diagnostico (mostrado no menu)
  int                      m_bbRawFreq = 0;
  bool bitbangOn() { return m_bitbangGen.enabled(); }
private:
  void bitbangSpeaker(bool level);
  void bitbangCheckIdle();
  AdLib                    m_adlib;
  bool                     m_adlibOn;

  int                      m_videoCard;
  int                      m_kbdLayout;

  bool                     m_timerSync;
  int64_t                  m_bbAvgDt = 0;   // bitbang: media do intervalo entre pulsos (us)     // Setup > Timer sync
  int                      m_lostIRQ0;      // IRQ0 do timer ainda nao entregues
  bool                     m_soundPaused;
  bool                     m_speakerAudible;  // frequencia do speaker dentro do que o gerador consegue tocar

  // XPC: EGA
  void    egaWrite(int offset, uint8_t value);
  uint8_t egaRead(int offset);
  uint32_t *               m_egaVRAM;       // 64K posicoes x 4 planos (byte p = plano p), na PSRAM
  uint32_t                 m_egaLatch32;
  // valores derivados dos registradores (recalculados em egaUpdateCache)
  uint32_t                 m_egaMapMask32, m_egaSR32, m_egaESR32, m_egaBM32, m_egaCC32, m_egaDC32;
  uint8_t                  m_egaFunc, m_egaRot, m_egaWMode, m_egaReadShift;
  bool                     m_egaReadMode1, m_egaFast;
  void egaUpdateCache();
  void egaUpdateGC(int index);
  uint8_t                  m_egaSeqIndex;
  uint8_t                  m_egaSeq[8];
  uint8_t                  m_egaGCIndex;
  uint8_t                  m_egaGC[16];
  uint8_t                  m_egaAttrIndex;
  bool                     m_egaAttrFlip;
  uint8_t                  m_egaAttr[0x20];
  bool                     m_egaActive;
  uint32_t *               m_egaShadow32;   // copia da pagina visivel (lida pelo video)
  int                      m_egaStart;      // endereco de inicio atual
  int                      m_egaShadowSize = EGA_SHADOWSIZE;   // posicoes de cada copia
  uint32_t *               m_egaSlot[2]      = { nullptr, nullptr };  // copias de paginas (0Dh: 2, 0Eh: 1)
  int                      m_egaSlotStart[2] = { -1, -1 };          // endereco de inicio de cada copia
  int                      m_egaSlots        = 1;
  int                      m_egaShown        = 0;                   // copia que esta na tela
  int                      m_ramKB = 640;
  bool                     m_bootMsgPT = true;
  volatile bool            m_postBeep = false;
  int64_t                  m_beepUntil = 0;
  uint8_t                  m_egaStartPending = 0;   // bit 0 = 0Ch gravado, bit 1 = 0Dh gravado
  uint8_t                  m_egaPendingAge   = 0;
  int                      m_egaMode = 0x0d;
  bool                     m_egaDirect = false;  // video lendo direto da PSRAM (experimento; desligado)
  bool                     m_egaHalf = false;    // modo 10h: copia so com as linhas pares
  bool                     m_egaCompact = false; // largura logica (CRTC 13h) diferente da visivel
  int                      m_egaW = 40;          // posicoes visiveis por linha (40 ou 80)
  int                      m_egaP = 40;          // posicoes por linha na memoria (CRTC 13h x 2)
  int                      m_egaLines = 200;
  unsigned                 m_egaSpan = 8000;     // m_egaLines * m_egaP
  void egaSetPitch(int value);
  int                      m_egaCharH = 8;       // altura do caractere da BIOS (8 ou 14)
  int                      m_egaBytesPerLine = 40;
  int                      m_egaPageSize = 0x2000;
  void egaStore(int offset, uint32_t value);
  void egaSetStart(int start);

  // CMOS & RTC
  MC146818                 m_MC146818;

  // extended I/O (MCP23S17)
  MCP23S17                 m_MCP23S17;
  uint8_t                  m_MCP23S17Sel;

  uint8_t                  m_bootDrive;

  SysReqCallback           m_sysReqCallback;

  char const *             m_baseDir;
  
  // serial ports
  PC8250                   m_COM1;
  PC8250                   m_COM2;

};
