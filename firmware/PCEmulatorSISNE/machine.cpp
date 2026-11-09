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


#include "machine.h"
#include "biosoffsets.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <math.h>


#pragma GCC optimize ("O3")




// CGA Craphics Card Ports Bits

#define CGA_MODECONTROLREG_TEXT80         0x01   // 0 = 40x25, 1 = 80x25
#define CGA_MODECONTROLREG_GRAPHICS       0x02   // 0 = text,  1 = graphics
#define CGA_MODECONTROLREG_COLOR          0x04   // 0 = color, 1 = monochrome
#define CGA_MODECONTROLREG_ENABLED        0x08   // 0 = video off, 1 = video on
#define CGA_MODECONTROLREG_GRAPH640       0x10   // 0 = 320x200 graphics, 1 = 640x200 graphics
#define CGA_MODECONTROLREG_BIT7BLINK      0x20   // 0 = text mode bit 7 controls background, 1 = text mode bit 7 controls blinking

#define CGA_COLORCONTROLREG_BACKCOLR_MASK 0x0f   // mask for 320x200 background color index (on 640x200 is the foreground)
#define CGA_COLORCONTROLREG_HIGHINTENSITY 0x10   // select high intensity colors
#define CGA_COLORCONTROLREG_PALETTESEL    0x20   // 0 is Green, red and brown, 1 is Cyan, magenta and white


// Hercules (HGC) Ports Bits

#define HGC_MODECONTROLREG_GRAPHICS       0x02   // 0 = text mode, 1 = graphics mode
#define HGC_MODECONTROLREG_ENABLED        0x08   // 0 = video off, 1 = video on
#define HGC_MODECONTROLREG_BIT7BLINK      0x20   // 0 = text mode bit 7 controls background, 1 = text mode bit 7 controls blinking
#define HGC_MODECONTROLREG_GRAPHICSPAGE   0x80   // 0 = graphics mapped on page 0 (0xB0000), 1 = graphics mapped on page 1 (0xB8000)

#define HGC_CONFSWITCH_ALLOWGRAPHICSMODE  0x01   // 0 = prevents graphics mode, 1 = allows graphics mode
#define HGC_CONFSWITCH_ALLOWPAGE1         0x02   // 0 = prevents access to page 1, 1 = allows access to page 1


// I/O expander (based on MCP23S17) ports

#define EXTIO_CONFIG                    0x00e0   // configuration port (see EXTIO_CONFIG_.... flags)
// whole 8 bit ports handling
#define EXTIO_DIRA                      0x00e1   // port A direction (0 = input, 1 = output)
#define EXTIO_DIRB                      0x00e2   // port B direction (0 = input, 1 = output)
#define EXTIO_PULLUPA                   0x00e3   // port A pullup enable (0 = disabled, 1 = enabled)
#define EXTIO_PULLUPB                   0x00e4   // port B pullup enable (0 = disabled, 1 = enabled)
#define EXTIO_PORTA                     0x00e5   // port A read/write
#define EXTIO_PORTB                     0x00e6   // port B read/write
// single GPIO handling
#define EXTIO_GPIOSEL                   0x00e7   // GPIO selection (0..7 = PA0..PA7, 8..15 = PB0..PB8)
#define EXTIO_GPIOCONF                  0x00e8   // selected GPIO direction and pullup (0 = input, 1 = output, 2 = input with pullup)
#define EXTIO_GPIO                      0x00e9   // selected GPIO read or write (0 = low, 1 = high)

// I/O expander configuration bits
#define EXTIO_CONFIG_AVAILABLE            0x01   // 1 = external IO available, 0 = not available
#define EXTIO_CONFIG_INT_POLARITY         0x02   // 1 = positive polarity, 0 = negative polarity (default)



// XPC: EGA
// expande 4 bits (um por plano) em 4 bytes 00h/FFh
static inline uint32_t egaExpand4(uint8_t v)
{
  return ((v & 1) ? 0x000000ff : 0) | ((v & 2) ? 0x0000ff00 : 0) | ((v & 4) ? 0x00ff0000 : 0) | ((v & 8) ? 0xff000000 : 0);
}



//////////////////////////////////////////////////////////////////////////////////////
// Machine


using fabgl::i8086;


uint8_t *         Machine::s_memory;
uint8_t *         Machine::s_videoMemory;


Machine::Machine() :
    #ifdef FABGL_EMULATED
    m_stepCallback(nullptr),
    #endif
    m_diskFilename(),
    m_disk(),
    m_diskChanged(),
    m_frameBuffer(nullptr),
    m_bootDrive(0),
    m_sysReqCallback(nullptr),
    m_baseDir(nullptr),
    m_adlibOn(false),
    m_videoCard(0),
    m_kbdLayout(0),
    m_lostIRQ0(0),
    m_timerSync(true),
    m_soundPaused(false),
    m_speakerAudible(true),
    m_egaVRAM(nullptr),
    m_egaLatch32(0),
    m_egaSeqIndex(0),
    m_egaSeq(),
    m_egaGCIndex(0),
    m_egaGC(),
    m_egaAttrIndex(0),
    m_egaAttrFlip(false),
    m_egaAttr(),
    m_egaActive(false),
    m_egaShadow32(nullptr),
    m_egaStart(0)
{
}


Machine::~Machine()
{
  for (int i = 0; i < DISKCOUNT; ++i) {
    free(m_diskFilename[i]);
    if (m_disk[i])
      fclose(m_disk[i]);
  }
  vTaskDelete(m_taskHandle);
  free(s_videoMemory);
}


void Machine::init()
{
  srand((uint32_t)time(NULL));

  // to avoid PSRAM bug without -mfix-esp32-psram-cache-issue
  // core 0 can only work reliably with the lower 2 MB and core 1 only with the higher 2 MB.
  s_memory = (uint8_t*)(SOC_EXTRAM_DATA_LOW + (xPortGetCoreID() == 1 ? 2 * 1024 * 1024 : 0));

  s_videoMemory = (uint8_t*)heap_caps_malloc(VIDEOMEMSIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

  // XPC: EGA com 256KB (4 planos de 64KB) na PSRAM, logo depois da RAM do PC (mesma metade da PSRAM
  //      usada por este core). O video nao le a PSRAM: le uma copia da pagina visivel (4 x 8KB) mantida
  //      em RAM interna, em B0000-B7FFF (sem Hercules essa area nao e usada).
  if (m_videoCard == 2 || m_videoCard == 3) {
    m_egaVRAM     = (uint32_t*)(s_memory + RAM_SIZE);
    m_egaShadow32 = (uint32_t*)s_videoMemory;
    m_egaSlot[0]  = m_egaSlot[1] = m_egaShadow32;
    m_egaSlotStart[0] = m_egaSlotStart[1] = -1;
    memset(m_egaVRAM, 0, EGA_PLANESIZE * 4);
    m_egaStart = 0;
    m_egaShadowSize = EGA_SHADOWSIZE;
    egaUpdateCache();
    printf("EGA: 256KB (PSRAM, 4 planos em 32 bits) + copia da pagina visivel (RAM interna)\n");
  }

  memset(s_memory, 0, RAM_SIZE);

  m_soundGen.play(true);
  m_soundGen.attach(&m_sinWaveGen);
  m_bitbangGen.setDutyCycle(4);    // pulso estreito: "zumbido" fino como no PC real
  m_bitbangGen.setVolume(55);
  m_bitbangGen.enable(false);
  m_soundGen.attach(&m_bitbangGen);
  m_pcmGen.setVolume(70);
  if (!m_pcmGen.begin())
    m_spkMode = 2;                 // sem memoria: fica no modo Classic
  m_pcmGen.setMute(m_spkMode != 1);
  m_pcmGen.enable(m_spkMode != 2);
  m_soundGen.attach(&m_pcmGen);
  if (m_adlibOn) {
    m_adlib.begin(FABGL_SOUNDGEN_DEFAULT_SAMPLE_RATE);
    m_adlib.enable(true);
    m_soundGen.attach(&m_adlib);
  }

  m_i8042.init();
  m_i8042.setCallbacks(this, keyboardInterrupt, mouseInterrupt, resetMachine, sysReq);
  m_i8042.setHotkeyCallbacks(menuHotkey, monitorModeHotkey);
  m_i8042.setPaletteHotkeys(paletteNextHotkey, paletteResetHotkey);

  m_PIT8253.setCallbacks(this, PITChangeOut);
  m_PIT8253.reset();

  m_MC146818.init("PCEmulator");
  m_MC146818.setCallbacks(this, MC146818Interrupt);

  m_MCP23S17.begin();
  m_MCP23S17Sel = 0;

  m_BIOS.init(this);

  i8086::setCallbacks(this, readPort, writePort, writeVideoMemory8, writeVideoMemory16, readVideoMemory8, readVideoMemory16, interrupt);
  i8086::setMemory(s_memory);

  m_reset = true;
}


void Machine::setDriveImage(int drive, char const * filename, int cylinders, int heads, int sectors)
{
  if (m_disk[drive]) {
    fclose(m_disk[drive]);
    m_disk[drive] = nullptr;
  }

  if (m_diskFilename[drive]) {
    free(m_diskFilename[drive]);
    m_diskFilename[drive] = nullptr;
  }

  m_BIOS.setDriveMediaType(drive, mediaUnknown);

  m_diskCylinders[drive] = cylinders;
  m_diskHeads[drive]     = heads;
  m_diskSectors[drive]   = sectors;
  
  m_diskChanged[drive]   = true;

  if (filename) {
    m_diskFilename[drive] = strdup(filename);
    m_disk[drive] = FileBrowser(m_baseDir).openFile(filename, "r+b");
    if (m_disk[drive]) {

      // get image file size
      fseek(m_disk[drive], 0L, SEEK_END);
      m_diskSize[drive] = ftell(m_disk[drive]);

      // need to detect geometry?
      if (cylinders == 0 || heads == 0 || sectors == 0)
        autoDetectDriveGeometry(drive);
    }
  }
}


void Machine::autoDetectDriveGeometry(int drive)
{
  // well known floppy formats
  static const struct {
    uint16_t tracks;
    uint8_t  sectors;
    uint8_t  heads;
  } FLOPPYFORMATS[] = {
    { 40,  8, 1 },   //  163840 bytes (160K, 5.25 inch)
    { 40,  9, 1 },   //  184320 bytes (180K, 5.25 inch)
    { 40,  8, 2 },   //  327680 bytes (320K, 5.25 inch)
    { 40,  9, 2 },   //  368640 bytes (360K, 5.25 inch)
    { 80,  9, 2 },   //  737280 bytes (720K, 3.5 inch)
    { 80, 15, 2 },   // 1228800 bytes (1200K, 5.25 inch)
    { 80, 18, 2 },   // 1474560 bytes (1440K, 3.5 inch)
    { 80, 36, 2 },   // 2949120 bytes (2880K, 3.5 inch)
  };

  // look for well known floppy formats
  for (auto const & ff : FLOPPYFORMATS) {
    if (512 * ff.tracks * ff.sectors * ff.heads == m_diskSize[drive]) {
      m_diskCylinders[drive] = ff.tracks;
      m_diskHeads[drive]     = ff.heads;
      m_diskSectors[drive]   = ff.sectors;
      //printf("autoDetectDriveGeometry, found floppy: t=%d s=%d h=%d\n", ff.tracks, ff.sectors, ff.heads);
      return;
    }
  }

  // maybe an hard disk, try to calculate geometry (max 528MB, common lower end for BIOS and MSDOS: https://tldp.org/HOWTO/Large-Disk-HOWTO-4.html)
  constexpr int MAXCYLINDERS = 1024;  // Cylinders : 1...1024
  constexpr int MAXHEADS     = 16;    // Heads     : 1...16 (actual limit is 256)
  constexpr int MAXSECTORS   = 63;    // Sectors   : 1...63
  int c = 1, h = 1;
  int s = (int)(m_diskSize[drive] / 512);
  if (s > MAXSECTORS) {
    h = s / MAXSECTORS;
    s = MAXSECTORS;
  }
  if (h > MAXHEADS) {
    c = h / MAXHEADS;
    h = MAXHEADS;
  }
  if (c > MAXCYLINDERS)
    c = MAXCYLINDERS;
  m_diskCylinders[drive] = c;
  m_diskHeads[drive]     = h;
  m_diskSectors[drive]   = s;
  //printf("autoDetectDriveGeometry, found HD: c=%d h=%d s=%d (tot=%d filesz=%d)\n", m_diskCylinders[drive], m_diskHeads[drive], m_diskSectors[drive], 512 * m_diskCylinders[drive] * m_diskHeads[drive] * m_diskSectors[drive], (int)m_diskSize[drive]);

}


void Machine::setCOM1(SerialPort * serialPort)
{
  m_COM1.setCallbacks(this, COM1Interrupt);
  m_COM1.setSerialPort(serialPort);
}


void Machine::setCOM2(SerialPort * serialPort)
{
  m_COM2.setCallbacks(this, COM2Interrupt);
  m_COM2.setSerialPort(serialPort);
}


void Machine::reset()
{
  m_reset = false;
  m_graphicsAdapter.setCGAPaletteShift(0);   // paleta CGA sempre volta para a do programa

  m_ticksCounter = 0;

  m_CGAMemoryOffset = 0;
  m_CGAModeReg      = 0;
  m_CGAColorReg     = 0;
  m_CGAVSyncQuery   = 0;

  m_HGCMemoryOffset = 0;
  m_HGCModeReg      = 0;
  m_HGCSwitchReg    = 0;
  m_HGCVSyncQuery   = 0;

  m_speakerDataEnable = false;
  m_bitbangGen.enable(false);
  m_bitbangFreq = 0;
  speakerEnableDisable();

  // XPC: reinicio "a frio": limpa a RAM do PC (vetores de interrupcao, area da BIOS, DOS e programas).
  //      Sem isso, um programa que trocou os vetores (ex.: Digger) deixa a maquina presa no reboot.
  memset(s_memory, 0, m_ramKB > 640 ? 0xb0000 : 0xa0000);

  // XPC: tamanho da RAM (256/512/640/704K): INT 12h e 40:13 dizem a memoria (1K a menos: a EBDA fica no topo)
  {
    int kb = (m_ramKB > 640 && m_egaVRAM) ? 640 : m_ramKB;
    uint8_t * rom = s_memory + BIOS_ADDR;
    int ebda = (kb - 1) * 64;
    rom[BIOSROM_MEMSIZE_OFF]      = (kb - 1) & 0xff;
    rom[BIOSROM_MEMSIZE_OFF + 1]  = (kb - 1) >> 8;
    rom[BIOSROM_EBDASEG_OFF]     = ebda & 0xff;
    rom[BIOSROM_EBDASEG_OFF + 1] = ebda >> 8;
    rom[BIOSROM_BOOTMSGPT_OFF]   = m_bootMsgPT;
  }

  m_i8042.reset();

  m_PIC8259A.reset();
  m_PIC8259B.reset();

  m_PIT8253.reset();
  m_PIT8253.setGate(0, true);
  m_lostIRQ0 = 0;

  // XPC: EGA volta ao estado de texto; marca a EGA na area de dados da BIOS
  egaLeave();
  m_egaAttrFlip = false;
  if (m_egaVRAM) {
    s_memory[0x487] = 0x60;
    s_memory[0x488] = 0x07;   // chaves: EGA com monitor colorido de 200 linhas (sem modos de 350 linhas)
  }
  //m_PIT8253.setGate(1, true); // @TODO: timer 1 used for DRAM refresh, required to run?

  m_MC146818.reset();

  if (m_adlibOn)
    m_adlib.reset();
  
  m_COM1.reset();
  m_COM2.reset();

  memset(m_CGA6845, 0, sizeof(m_CGA6845));
  memset(m_HGC6845, 0, sizeof(m_HGC6845));

  m_BIOS.reset();

  i8086::reset();

  if (m_postBeep) {
    m_postBeep = false;
    m_sinWaveGen.setFrequency(900);
    m_sinWaveGen.enable(true);
    m_beepUntil = esp_timer_get_time() + 150000;
  }

  // set boot drive (0, 1, 0x80, 0x81)
  i8086::setDL((m_bootDrive & 1) | (m_bootDrive > 1 ? 0x80 : 0x00));
}


void Machine::setBootMsgPT(bool value)
{
  m_bootMsgPT = value;
  if (s_memory && !m_reset)
    s_memory[BIOS_ADDR + BIOSROM_BOOTMSGPT_OFF] = value;
}


void Machine::run()
{
  // XPC: pilha maior (o menu, o gerenciador de arquivos e a criacao de discos rodam nesta task)
  xTaskCreatePinnedToCore(&runTask, "", 6144, this, 5, &m_taskHandle, CoreUsage::quietCore());
}


void Machine::runTask(void * pvParameters)
{
  auto m = (Machine*)pvParameters;

  m->init();

	while (true) {

    if (m->m_reset)
      m->reset();

    #ifdef FABGL_EMULATED
    taskEmuCheck();
    if (m->m_stepCallback)
      m->m_stepCallback(m);
    #endif

    i8086::step();
		m->tick();

	}
}


void Machine::tick()
{
  ++m_ticksCounter;

  if ((m_ticksCounter & 0x03) == 0x03) {
  
    if (m_COM1.assigned())
      m_COM1.tick();
    if (m_COM2.assigned())
      m_COM2.tick();
      
    if ((m_ticksCounter & 0x7f) == 0x7f) {
      m_PIT8253.tick();
      bitbangCheckIdle();
      if (m_beepUntil && esp_timer_get_time() > m_beepUntil) {
        m_beepUntil = 0;
        speakerEnableDisable();
      }
      // EGA: endereco de inicio com so um byte gravado ha um tempo: aplica assim mesmo
      if (m_egaStartPending && ++m_egaPendingAge >= 4) {
        m_egaStartPending = 0;
        egaSetStart((m_CGA6845[0x0c] << 8) | m_CGA6845[0x0d]);
      }
      // run keyboard controller every PIT tick (just to not overload CPU with continous checks)
      m_i8042.tick();
    }
    
  }
  
  // XPC: entrega IRQ0 atrasados assim que o anterior terminar
  //      (nunca com tecla esperando: o IRQ1 tem prioridade menor e ficava sem vez - teclado do CP/M-86)
  if (m_lostIRQ0 && !m_i8042.outputFull() && !m_PIC8259A.pendingInterrupt() && m_PIC8259A.signalInterrupt(0))
    --m_lostIRQ0;

  if (m_PIC8259A.pendingInterrupt() && i8086::IRQ(m_PIC8259A.pendingInterruptNum()))
    m_PIC8259A.ackPendingInterrupt();
  if (m_PIC8259B.pendingInterrupt() && i8086::IRQ(m_PIC8259B.pendingInterruptNum()))
    m_PIC8259B.ackPendingInterrupt();

}


void Machine::setCGA6845Register(uint8_t value)
{
  m_CGA6845[m_CGA6845SelectRegister] = value;

  switch (m_CGA6845SelectRegister) {

    // cursor start: (bits 5,6 = cursor blink and visibility control), bits 0..4 cursor start scanline
    case 0x0a:
      m_graphicsAdapter.setCursorVisible((m_CGA6845[0xa] >> 5) >= 2);
      // no break!
    // cursor end: bits 0..4 cursor end scanline
    case 0x0b:
      m_graphicsAdapter.setCursorShape(2 * (m_CGA6845[0xa] & 0x1f), 2 * (m_CGA6845[0xb] & 0x1f));
      break;

    // video memory start offset (0x0c = H, 0x0d = L)
    case 0x0c:
    case 0x0d:
      // XPC: o CGA tem 16KB; sem a mascara, um endereco de inicio alto faz o video ler alem do buffer (reset)
      m_CGAMemoryOffset = (((m_CGA6845[0xc] << 8) | m_CGA6845[0xd]) << 1) & 0x3fff;
      setCGAMode();
      break;

    // cursor position (0x0e = H and 0x0f = L)
    case 0x0e:
    case 0x0f:
    {
      int pos = (m_CGA6845[0xe] << 8) | m_CGA6845[0xf];
      m_graphicsAdapter.setCursorPos(pos / m_graphicsAdapter.getTextColumns(), pos % m_graphicsAdapter.getTextColumns());
      break;
    }

  }
}


void Machine::setCGAMode()
{
  if ((m_CGAModeReg & CGA_MODECONTROLREG_ENABLED) == 0) {

    // video disabled
    // XPC: nao desliga o sinal VGA. Programas como o WordStar desligam o video (bit 3 do 3D8/3B8)
    //      a cada atualizacao da tela para evitar "chuvisco" no CGA real; aqui isso parava o sinal e
    //      o monitor perdia o sincronismo a cada tecla. A imagem simplesmente continua na tela.

  } else if ((m_CGAModeReg & CGA_MODECONTROLREG_TEXT80) == 0 && (m_CGAModeReg & CGA_MODECONTROLREG_GRAPHICS) == 0) {

    // 40 column text mode
    //printf("CGA, 40 columns text mode\n");
    m_frameBuffer = s_videoMemory + 0x8000 + m_CGAMemoryOffset;
    m_graphicsAdapter.setVideoBuffer(m_frameBuffer);
    m_graphicsAdapter.setEmulation(GraphicsAdapter::Emulation::PC_Text_40x25_16Colors);
    m_graphicsAdapter.setBit7Blink(m_CGAModeReg & CGA_MODECONTROLREG_BIT7BLINK);
    m_graphicsAdapter.enableVideo(true);

  } else if ((m_CGAModeReg & CGA_MODECONTROLREG_TEXT80) && (m_CGAModeReg & CGA_MODECONTROLREG_GRAPHICS) == 0) {

    // 80 column text mode
    //printf("CGA, 80 columns text mode\n");
    m_frameBuffer = s_videoMemory + 0x8000 + m_CGAMemoryOffset;
    m_graphicsAdapter.setVideoBuffer(m_frameBuffer);
    m_graphicsAdapter.setEmulation(GraphicsAdapter::Emulation::PC_Text_80x25_16Colors);
    m_graphicsAdapter.setBit7Blink(m_CGAModeReg & CGA_MODECONTROLREG_BIT7BLINK);
    m_graphicsAdapter.enableVideo(true);

  } else if ((m_CGAModeReg & CGA_MODECONTROLREG_GRAPHICS) && (m_CGAModeReg & CGA_MODECONTROLREG_GRAPH640) == 0) {

    // 320x200 graphics
    //printf("CGA, 320x200 graphics mode\n");
    m_frameBuffer = s_videoMemory + 0x8000 + m_CGAMemoryOffset;
    m_graphicsAdapter.setVideoBuffer(m_frameBuffer);
    m_graphicsAdapter.setEmulation(GraphicsAdapter::Emulation::PC_Graphics_320x200_4Colors);
    int paletteIndex = (bool)(m_CGAColorReg & CGA_COLORCONTROLREG_PALETTESEL) * 2 + (bool)(m_CGAColorReg & CGA_COLORCONTROLREG_HIGHINTENSITY);
    // bit BW (modo 5): paleta ciano/vermelho/branco, como no CGA real
    if (m_CGAModeReg & CGA_MODECONTROLREG_COLOR)
      paletteIndex = 4 + (bool)(m_CGAColorReg & CGA_COLORCONTROLREG_HIGHINTENSITY);
    m_graphicsAdapter.setPCGraphicsPaletteInUse(paletteIndex);
    m_graphicsAdapter.setPCGraphicsBackgroundColorIndex(m_CGAColorReg & CGA_COLORCONTROLREG_BACKCOLR_MASK);
    m_graphicsAdapter.enableVideo(true);

  } else if ((m_CGAModeReg & CGA_MODECONTROLREG_GRAPHICS) && (m_CGAModeReg & CGA_MODECONTROLREG_GRAPH640)) {

    // 640x200 graphics
    //printf("CGA, 640x200 graphics mode\n");
    m_frameBuffer = s_videoMemory + 0x8000 + m_CGAMemoryOffset;
    m_graphicsAdapter.setVideoBuffer(m_frameBuffer);
    m_graphicsAdapter.setEmulation(GraphicsAdapter::Emulation::PC_Graphics_640x200_2Colors);
    m_graphicsAdapter.setPCGraphicsForegroundColorIndex(m_CGAColorReg & CGA_COLORCONTROLREG_BACKCOLR_MASK);
    m_graphicsAdapter.enableVideo(true);

  }
}


void Machine::setHGC6845Register(uint8_t value)
{
  m_HGC6845[m_HGC6845SelectRegister] = value;
  switch (m_HGC6845SelectRegister) {

    // cursor start: (bits 5,6 = cursor blink and visibility control), bits 0..4 cursor start scanline
    case 0x0a:
      m_graphicsAdapter.setCursorVisible((m_HGC6845[0xa] >> 5) >= 2);
    // cursor end: bits 0..4 cursor end scanline
    case 0x0b:
      m_graphicsAdapter.setCursorShape((m_HGC6845[0xa] & 0x1f), (m_HGC6845[0xb] & 0x1f));
      break;

    // video memory start offset (0x0c = H, 0x0d = L)
    case 0x0c:
    case 0x0d:
      m_HGCMemoryOffset = ((m_HGC6845[0xc] << 8) | m_HGC6845[0xd]) << 1;
      setHGCMode();
      break;

    // cursor position (0x0e = H and 0x0f = L)
    case 0x0e:
    case 0x0f:
    {
      int pos = (m_HGC6845[0xe] << 8) | m_HGC6845[0xf];
      m_graphicsAdapter.setCursorPos(pos / m_graphicsAdapter.getTextColumns(), pos % m_graphicsAdapter.getTextColumns());
      break;
    }

  }
}


void Machine::setHGCMode()
{
  constexpr int HGC_OFFSET_PAGE0 = 0x0000;
  constexpr int HGC_OFFSET_PAGE1 = 0x8000;

  if ((m_HGCModeReg & HGC_MODECONTROLREG_ENABLED) == 0) {

    // video disabled
    // XPC: nao desliga o sinal VGA. Programas como o WordStar desligam o video (bit 3 do 3D8/3B8)
    //      a cada atualizacao da tela para evitar "chuvisco" no CGA real; aqui isso parava o sinal e
    //      o monitor perdia o sincronismo a cada tecla. A imagem simplesmente continua na tela.

  } else if ((m_HGCModeReg & HGC_MODECONTROLREG_GRAPHICS) == 0 || (m_HGCSwitchReg & HGC_CONFSWITCH_ALLOWGRAPHICSMODE) == 0) {

    // XPC: na configuracao Hercules a BIOS usa o texto do CGA; ao sair do grafico volta para a tela do CGA
    if (m_videoCard == 1) {
      setCGAMode();
      return;
    }

    // text mode
    //printf("Hercules, text mode\n");
    m_frameBuffer = s_videoMemory + HGC_OFFSET_PAGE0;
    m_graphicsAdapter.setVideoBuffer(m_frameBuffer);
    m_graphicsAdapter.setEmulation(GraphicsAdapter::Emulation::PC_Text_80x25_16Colors);
    m_graphicsAdapter.setBit7Blink(m_HGCModeReg & HGC_MODECONTROLREG_BIT7BLINK);
    m_graphicsAdapter.enableVideo(true);

  } else if ((m_HGCModeReg &HGC_MODECONTROLREG_GRAPHICS)) {

    // graphics mode
    //printf("Hercules, graphics mode\n");
    int offset = (m_HGCModeReg & HGC_MODECONTROLREG_GRAPHICSPAGE) && (m_HGCSwitchReg & HGC_CONFSWITCH_ALLOWPAGE1) ? HGC_OFFSET_PAGE1 : HGC_OFFSET_PAGE0;
    m_frameBuffer = s_videoMemory + offset;
    m_graphicsAdapter.setVideoBuffer(m_frameBuffer);
    m_graphicsAdapter.setEmulation(GraphicsAdapter::Emulation::PC_Graphics_HGC_720x348);
    m_graphicsAdapter.enableVideo(true);

  }

}


// Hercules em modo grafico ativo na tela (configuracao Hercules)
// enquanto isso, escritas nas portas do CGA nao trocam o modo de video
bool Machine::HGCGraphicsActive()
{
  return m_videoCard == 1 &&
         (m_HGCModeReg & HGC_MODECONTROLREG_ENABLED) &&
         (m_HGCModeReg & HGC_MODECONTROLREG_GRAPHICS) &&
         (m_HGCSwitchReg & HGC_CONFSWITCH_ALLOWGRAPHICSMODE);
}


void Machine::writePort(void * context, int address, uint8_t value)
{
  auto m = (Machine*)context;

  // placa CGA: as portas da Hercules (0x3b0..0x3bf) nao existem
  if (m->m_videoCard != 1 && address >= 0x3b0 && address <= 0x3bf)
    return;

  switch (address) {

    // AdLib (OPL2)
    case 0x388:
    case 0x389:
      if (m->m_adlibOn)
        m->m_adlib.write(address, value);
      break;

    // PIC8259A
    case 0x20:
    case 0x21:
      m->m_PIC8259A.write(address & 1, value);
      break;

    // PIC8259B
    case 0xa0:
    case 0xa1:
      m->m_PIC8259B.write(address & 1, value);
      break;

    // PIT8253
    case 0x0040:
    case 0x0041:
    case 0x0042:
    case 0x0043:
      m->m_PIT8253.write(address & 3, value);
      if ((address == 0x43 && (value >> 6) == 2) || address == 0x42)
        m->speakerSetFreq();
      break;

    // 8042 keyboard controller input
    case 0x0060:
      m->m_i8042.write(0, value);
      break;

    // PortB
    //   bit 1 : speaker data enable
    //   bit 0 : timer 2 gate
    case 0x0061:
      m->m_speakerDataEnable = value & 0x02;
      m->m_PIT8253.setGate(2, value & 0x01);
      m->speakerEnableDisable();
      // XPC: com o timer 2 parado, o bit 1 move o alto-falante diretamente (som "bit-bang")
      //      Digger le a porta 61h e liga so o bit 1 com o timer 2 ativo em ~30KHz (inaudivel): no PC real o
      //      alto-falante = bit 1 AND saida do timer 2, entao cada pulso vira um "clique"
      m->bitbangSpeaker((value & 0x02) && (!(value & 0x01) || !m->m_speakerAudible));
      break;

    // 8042 keyboard controller input
    case 0x0064:
      m->m_i8042.write(1, value);
      break;

    // MC146818 RTC & RAM
    case 0x0070:
    case 0x0071:
      m->m_MC146818.write(address & 1, value);
      break;
      
    // COM2
    case 0x2f8 ... 0x2ff:
      if (m->m_COM2.assigned())
        m->m_COM2.write(address, value);
      break;

    // CGA - CRT 6845 - register selection register
    case 0x3d4:
      m->m_CGA6845SelectRegister = value;
      break;

    // XPC: EGA - controlador de atributos (indice/dado alternados), sequenciador e controlador grafico
    case 0x3c0:
      if (m->m_egaVRAM) {
        if (!m->m_egaAttrFlip)
          m->m_egaAttrIndex = value & 0x1f;
        else {
          m->m_egaAttr[m->m_egaAttrIndex] = value;
          if (m->m_egaAttrIndex < 16)
            m->m_graphicsAdapter.setEGAPaletteItem(m->m_egaAttrIndex, value);
        }
        m->m_egaAttrFlip = !m->m_egaAttrFlip;
      }
      break;
    case 0x3c4:
      m->m_egaSeqIndex = value & 7;
      break;
    case 0x3c5:
      m->m_egaSeq[m->m_egaSeqIndex] = value;
      if (m->m_egaVRAM && m->m_egaSeqIndex == 2)
        m->m_egaMapMask32 = egaExpand4(value & 0x0f);
      break;
    case 0x3ce:
      m->m_egaGCIndex = value & 15;
      break;
    case 0x3cf:
      m->m_egaGC[m->m_egaGCIndex] = value;
      if (m->m_egaVRAM)
        m->egaUpdateGC(m->m_egaGCIndex);
      break;

    // CGA - CRT 6845 - selected register write
    case 0x3d5:
      if (m->m_egaActive) {
        // EGA: so o endereco de inicio importa (troca de pagina / rolagem)
        m->m_CGA6845[m->m_CGA6845SelectRegister & 0x1f] = value;
        if (m->m_CGA6845SelectRegister == 0x13)
          m->egaSetPitch(value);
        // o endereco so vale quando o byte baixo (0Dh) e escrito, ou na proxima leitura de 3DAh
        // (a EGA real so troca no retraco vertical; trocar no byte alto mostraria uma pagina deslocada)
        //      Ha jogos que gravam 0Dh antes de 0Ch (ex.: Tetris): so aplica quando os DOIS bytes foram gravados
        if (m->m_CGA6845SelectRegister == 0x0c || m->m_CGA6845SelectRegister == 0x0d) {
          m->m_egaStartPending |= (m->m_CGA6845SelectRegister == 0x0c ? 1 : 2);
          m->m_egaPendingAge = 0;
          if (m->m_egaStartPending == 3) {
            m->m_egaStartPending = 0;
            m->egaSetStart((m->m_CGA6845[0x0c] << 8) | m->m_CGA6845[0x0d]);
          }
        }
      } else if (m->HGCGraphicsActive())
        m->m_CGA6845[m->m_CGA6845SelectRegister] = value;   // Hercules grafica na tela: CGA nao toma a tela
      else
        m->setCGA6845Register(value);
      break;

    // CGA - Mode Control Register
    case 0x3d8:
      m->m_CGAModeReg = value;
      // XPC: a EGA nao tem este registrador; em modo grafico EGA a escrita nao muda a tela
      if (!m->HGCGraphicsActive() && !m->m_egaActive)
        m->setCGAMode();
      break;

    // CGA - Color Select register
    case 0x3d9:
      m->m_CGAColorReg = value;
      if (!m->HGCGraphicsActive() && !m->m_egaActive)
        m->setCGAMode();
      break;

    // Hercules (HGC) - CRT 6845 - register selection register
    case 0x3b4:
      m->m_HGC6845SelectRegister = value;
      break;

    // Hercules (HGC) - CRT 6845 - selected register write
    case 0x3b5:
      m->setHGC6845Register(value);
      break;

    // Hercules (HGC) - Display Mode Control Port
    case 0x3b8:
      m->m_HGCModeReg = value;
      m->setHGCMode();
      break;

    // Hercules (HGC) - Configuration Switch
    case 0x3bf:
      m->m_HGCSwitchReg = value;
      m->setHGCMode();
      break;
      
    // COM1
    case 0x3f8 ... 0x3ff:
      if (m->m_COM1.assigned())
        m->m_COM1.write(address, value);
      break;

    // I/O expander - Configuration
    case EXTIO_CONFIG:
      m->m_MCP23S17.setINTActiveHigh(value & EXTIO_CONFIG_INT_POLARITY);
      break;

    // I/O expander - Port A/B Direction
    case EXTIO_DIRA ... EXTIO_DIRB:
      m->m_MCP23S17.setPortDir(address - EXTIO_DIRA + MCP_PORTA, ~value);
      printf("dir %d = %02X\n", address - EXTIO_DIRA + MCP_PORTA, ~value);
      break;

    // I/O expander - Port A/B pullup
    case EXTIO_PULLUPA ... EXTIO_PULLUPB:
      m->m_MCP23S17.enablePortPullUp(address - EXTIO_PULLUPA + MCP_PORTA, value);
      break;

    // I/O expander - Port A/B write
    case EXTIO_PORTA ... EXTIO_PORTB:
      m->m_MCP23S17.writePort(address - EXTIO_PORTA + MCP_PORTA, value);
      printf("set %d = %02X\n", address - EXTIO_PORTA + MCP_PORTA, value);
      break;

    // I/O expander - GPIO selection
    case EXTIO_GPIOSEL:
      m->m_MCP23S17Sel = value & 0xf;
      break;

    // I/O expander - GPIO direction and pullup
    case EXTIO_GPIOCONF:
      m->m_MCP23S17.configureGPIO(m->m_MCP23S17Sel, value & 1 ? fabgl::MCPDir::Output : fabgl::MCPDir::Input, value & 2);
      break;

    // I/O expander - GPIO write
    case EXTIO_GPIO:
      m->m_MCP23S17.writeGPIO(m->m_MCP23S17Sel, value);
      break;

    default:
      //printf("OUT %04x=%02x\n", address, value);
      break;
  }
}


uint8_t Machine::readPort(void * context, int address)
{
  auto m = (Machine*)context;

  // placa CGA: as portas da Hercules (0x3b0..0x3bf) nao existem (programas nao detectam Hercules)
  if (m->m_videoCard != 1 && address >= 0x3b0 && address <= 0x3bf)
    return 0xff;

  switch (address) {

    // AdLib (OPL2)
    case 0x388:
    case 0x389:
      return m->m_adlibOn ? m->m_adlib.read(address) : 0xff;

    // PIC8259A
    case 0x0020:
    case 0x0021:
      return m->m_PIC8259A.read(address & 1);

    // PIC8259B
    case 0x00a0:
    case 0x00a1:
      return m->m_PIC8259B.read(address & 1);

    // PIT8253
    case 0x0040:
    case 0x0041:
    case 0x0042:
    case 0x0043:
      return m->m_PIT8253.read(address & 3);

    // 8042 keyboard controller output
    case 0x0060:
      return m->m_i8042.read(0);

    // Port B
    //   bit 5 : timer 2 out
    //   bit 4 : toggles every 15.085us (DMA refresh)
    //   bit 1 : speaker data enable
    //   bit 0 : timer 2 gate
    case 0x0061:
      return ((int)m->m_PIT8253.getOut(2) << 5) |    // bit 5
             (esp_timer_get_time() & 0x10)       |   // bit 4 (toggles every 16us)
             ((int)m->m_speakerDataEnable << 1)  |   // bit 1
             ((int)m->m_PIT8253.getGate(2));         // bit 0

    // I/O port
    case 0x0062:
      return 0x20 * m->m_PIT8253.getOut(2);  // bit 5 = timer 2 output

    // 8042 keyboard controller status register
    case 0x0064:
      return m->m_i8042.read(1);

    // MC146818 RTC & RAM
    case 0x0070:
    case 0x0071:
      return m->m_MC146818.read(address & 1);

    // COM2
    case 0x2f8 ... 0x2ff:
      return m->m_COM2.assigned() ? m->m_COM2.read(address) : 0;

    // XPC: EGA - leituras (a EGA real nao permite ler quase nada; devolve o ultimo valor)
    case 0x3c1:
      return m->m_egaAttr[m->m_egaAttrIndex];
    case 0x3c2:
      return 0x00;
    case 0x3c5:
      return m->m_egaSeq[m->m_egaSeqIndex];
    case 0x3cf:
      return m->m_egaGC[m->m_egaGCIndex];

    // CGA - CRT 6845 - register selection register
    case 0x3d4:
      return 0x00;  // not readable

    // CGA - CRT 6845 - selected register read
    case 0x3d5:
      return m->m_CGA6845SelectRegister >= 14 && m->m_CGA6845SelectRegister < 16 ? m->m_CGA6845[m->m_CGA6845SelectRegister] : 0x00;

    // CGA - Color Select register
    // note: this register should be write-only, but some games do not work if it isn't readable
    case 0x3d9:
      return m->m_CGAColorReg;

    // CGA - Status Register
    // real vertical sync is too fast for our slowly emulated 8086, so
    // here it is just a fake, just to allow programs that check it to keep going anyway.
    case 0x3da:
      if (m->m_egaStartPending) {
        m->m_egaStartPending = 0;
        m->egaSetStart((m->m_CGA6845[0x0c] << 8) | m->m_CGA6845[0x0d]);
      }
      m->m_egaAttrFlip = false;   // ler 3DA reinicia o flip-flop do controlador de atributos (EGA)
      m->m_CGAVSyncQuery += 1;
      return (m->m_CGAVSyncQuery & 0x7) != 0 ? 0x09 : 0x00; // "not VSync" (0x00) every 7 queries

    // Hercules (HGC) - register selection register
    case 0x3b4:
      return 0x00;  // not readable

    // Hercules (HGC) - selected register read
    case 0x3b5:
      return m->m_HGC6845SelectRegister >= 14 && m->m_HGC6845SelectRegister < 16 ? m->m_HGC6845[m->m_HGC6845SelectRegister] : 0x00;

    // Hercules (HGC) - Display Status Port
    // real vertical sync is too fast for our slowly emulated 8086, so
    // here it is just a fake, just to allow programs that check it to keep going anyway.
    case 0x3ba:
      m->m_HGCVSyncQuery += 1;
      return (m->m_HGCVSyncQuery & 0x7) != 0 ? 0x00 : 0x80; // "not VSync" (0x80) every 7 queries

    // COM1
    case 0x3f8 ... 0x3ff:
      return m->m_COM1.assigned() ? m->m_COM1.read(address) : 0;

    // I/O expander - Configuration
    case EXTIO_CONFIG:
      return (m->m_MCP23S17.available()        ? EXTIO_CONFIG_AVAILABLE    : 0) |
             (m->m_MCP23S17.getINTActiveHigh() ? EXTIO_CONFIG_INT_POLARITY : 0);

    // I/O expander - Port A/B Direction
    case EXTIO_DIRA ... EXTIO_DIRB:
      return m->m_MCP23S17.getPortDir(address - EXTIO_DIRA + MCP_PORTA);

    // I/O expander - Port A/B pullup
    case EXTIO_PULLUPA ... EXTIO_PULLUPB:
      return m->m_MCP23S17.getPortPullUp(address - EXTIO_PULLUPA + MCP_PORTA);

    // I/O expander - Port A/B read
    case EXTIO_PORTA ... EXTIO_PORTB:
      return m->m_MCP23S17.readPort(address - EXTIO_PORTA + MCP_PORTA);

    // I/O expander - GPIO selection
    case EXTIO_GPIOSEL:
      return m->m_MCP23S17Sel;

    // I/O expander - GPIO read
    case EXTIO_GPIO:
      return m->m_MCP23S17.readGPIO(m->m_MCP23S17Sel);

  }

  //printf("IN %04X\n", address);
  return 0xff;
}


void Machine::PITChangeOut(void * context, int timerIndex)
{
  auto m = (Machine*)context;

  // timer 0 trigged?
  if (timerIndex == 0 &&  m->m_PIT8253.getOut(0) == true) {
    // yes, report 8259A-IR0 (IRQ0, INT 08h)
    // XPC: se o IRQ0 anterior ainda esta sendo atendido, guarda para entregar depois
    //      (evita que musicas/jogos guiados pelo timer oscilem de velocidade)
    //      so para timer ate ~2500Hz (musica); taxas altas (som digitalizado) nao acumulam
    int count = m->m_PIT8253.timerInfo(0).resetCount;
    // so com o timer reprogramado (musica): na taxa padrao de 18,2Hz (relogio do sistema) nao acumula
    bool slowTimer = (count != 0 && count >= PIT_TICK_FREQ / 2500);
    if (!m->m_PIC8259A.signalInterrupt(0) && slowTimer && m->m_timerSync && m->m_lostIRQ0 < 4)
      ++m->m_lostIRQ0;
  }
}


// reset from 8042
bool Machine::resetMachine(void * context)
{
  auto m = (Machine*)context;
  m->trigReset();
  return true;
}

// CTRL + ALT + UP: troca modo do monitor (colorido / verde / ambar / branco)
bool Machine::monitorModeHotkey(void * context)
{
  auto m = (Machine*)context;
  m->m_graphicsAdapter.cycleMonitorMode();
  return true;
}

// CTRL + ALT + RIGHT: proxima paleta CGA (so muda a cor na tela, o programa nao sabe)
bool Machine::paletteNextHotkey(void * context)
{
  auto m = (Machine*)context;
  m->m_graphicsAdapter.setCGAPaletteShift(m->m_graphicsAdapter.CGAPaletteShift() + 1);
  return true;
}

// CTRL + ALT + LEFT: volta para a paleta escolhida pelo programa
bool Machine::paletteResetHotkey(void * context)
{
  auto m = (Machine*)context;
  m->m_graphicsAdapter.setCGAPaletteShift(0);
  return true;
}

// CTRL + ALT + UP: abre o mesmo menu do SYSREQ
bool Machine::menuHotkey(void * context)
{
  auto m = (Machine*)context;
  if (m->m_sysReqCallback)
    m->m_sysReqCallback();
  return true;
}

// SYSREQ (ALT + PRINTSCREEN)
bool Machine::sysReq(void * context)
{
  auto m = (Machine*)context;
  if (m->m_sysReqCallback)
    m->m_sysReqCallback();
  return true;
}


// 8259A-IR1 (IRQ1, INT 09h)
bool Machine::keyboardInterrupt(void * context)
{
  auto m = (Machine*)context;
  return m->m_PIC8259A.signalInterrupt(1);
}


// 8259B-IR4 (IRQ12, INT 074h)
bool Machine::mouseInterrupt(void * context)
{
  auto m = (Machine*)context;
  return m->m_PIC8259B.signalInterrupt(4);
}


// interrupt from MC146818, trig 8259B-IR0 (IRQ8, INT 70h)
bool Machine::MC146818Interrupt(void * context)
{
  auto m = (Machine*)context;
  return m->m_PIC8259B.signalInterrupt(0);
}


// interrupt from COM1, trig 8259A-IR4 (IRQ4, INT 0Ch)
bool Machine::COM1Interrupt(PC8250 * source, void * context)
{
  auto m = (Machine*)context;
  return source->getOut2() && m->m_PIC8259A.signalInterrupt(4);
}


// interrupt from COM2, trig 8259A-IR3 (IRQ3, INT 0Bh)
bool Machine::COM2Interrupt(PC8250 * source, void * context)
{
  auto m = (Machine*)context;
  return source->getOut2() && m->m_PIC8259A.signalInterrupt(3);
}


void Machine::writeVideoMemory8(void * context, int address, uint8_t value)
{
  //printf("WVMEM %05X <= %02X\n", address, value);
  if (address >= 0xb0000) {
    auto m = (Machine*)context;
    // EGA: B0000-B7FFF guarda a copia visivel; em modo grafico EGA a area B nao e mapeada
    if (!m->m_egaVRAM || (address >= 0xb8000 && !m->m_egaActive))
      s_videoMemory[address - 0xb0000] = value;
  }
  else {
    auto m = (Machine*)context;
    if (m->m_egaVRAM)
      m->egaWrite(address - 0xa0000, value);
    else if (m->m_ramKB > 640)
      s_memory[address] = value;    // 704K: A0000-AFFFF e RAM (so sem EGA)
  }
}


void Machine::writeVideoMemory16(void * context, int address, uint16_t value)
{
  //printf("WVMEM %05X <= %04X\n", address, value);
  if (address >= 0xb0000) {
    auto m = (Machine*)context;
    if (!m->m_egaVRAM || (address >= 0xb8000 && !m->m_egaActive))
      *(uint16_t*)(s_videoMemory + address - 0xb0000) = value;
  }
  else {
    auto m = (Machine*)context;
    if (m->m_egaVRAM) {
      m->egaWrite(address - 0xa0000, value & 0xff);
      m->egaWrite(address - 0xa0000 + 1, value >> 8);
    } else if (m->m_ramKB > 640)
    { s_memory[address] = value & 0xff; s_memory[address + 1] = value >> 8; }
  }
}


uint8_t Machine::readVideoMemory8(void * context, int address)
{
  if (address >= 0xb0000)
    return s_videoMemory[address - 0xb0000];
  auto m = (Machine*)context;
  if (m->m_egaVRAM)
    return m->egaRead(address - 0xa0000);
  if (m->m_ramKB > 640)
    return s_memory[address];
  return 0xff;
}


uint16_t Machine::readVideoMemory16(void * context, int address)
{
  if (address >= 0xb0000)
    return *(uint16_t*)(s_videoMemory + address - 0xb0000);
  auto m = (Machine*)context;
  if (m->m_egaVRAM) {
    uint8_t lo = m->egaRead(address - 0xa0000);
    uint8_t hi = m->egaRead(address - 0xa0000 + 1);
    return lo | (hi << 8);
  }
  if (m->m_ramKB > 640)
    return s_memory[address] | (s_memory[address + 1] << 8);
  return 0xffff;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// XPC: EGA (modo 0Dh, 320x200x16)
//
// Memoria: 64K posicoes de 32 bits na PSRAM; byte p de cada posicao = plano p.
// Assim cada acesso do programa a A000:xxxx faz UMA leitura/escrita de 32 bits, e as operacoes
// do controlador grafico (set/reset, ALU, mascara de bits, mascara de planos) sao feitas nos 4
// planos de uma vez. O video le uma copia da pagina visivel (8K posicoes = 32KB, RAM interna).


// atualiza so o que depende do registrador do controlador grafico que mudou (chamado milhoes de vezes)
void Machine::egaUpdateGC(int index)
{
  uint8_t v = m_egaGC[index];
  switch (index) {
    case 0: m_egaSR32  = egaExpand4(v & 0x0f); break;
    case 1: m_egaESR32 = egaExpand4(v & 0x0f); break;
    case 2: m_egaCC32  = egaExpand4(v & 0x0f); break;
    case 3: m_egaFunc  = (v >> 3) & 3; m_egaRot = v & 7; break;
    case 4: m_egaReadShift = (v & 3) * 8; break;
    case 5: m_egaWMode = v & 3; m_egaReadMode1 = v & 0x08; break;
    case 7: m_egaDC32  = egaExpand4(v & 0x0f); break;
    case 8: m_egaBM32  = v * 0x01010101u; break;
    default: return;
  }
  m_egaFast = (m_egaWMode == 0 && (m_egaGC[1] & 0x0f) == 0 && m_egaGC[3] == 0 && m_egaGC[8] == 0xff);
}


// recalcula os valores derivados dos registradores (chamado quando sequenciador/controlador grafico mudam)
void Machine::egaUpdateCache()
{
  m_egaMapMask32 = egaExpand4(m_egaSeq[2] & 0x0f);
  m_egaSR32      = egaExpand4(m_egaGC[0] & 0x0f);
  m_egaESR32     = egaExpand4(m_egaGC[1] & 0x0f);
  m_egaBM32      = m_egaGC[8] * 0x01010101u;
  m_egaFunc      = (m_egaGC[3] >> 3) & 3;
  m_egaRot       = m_egaGC[3] & 7;
  m_egaWMode     = m_egaGC[5] & 3;
  m_egaCC32      = egaExpand4(m_egaGC[2] & 0x0f);
  m_egaDC32      = egaExpand4(m_egaGC[7] & 0x0f);
  m_egaReadMode1 = m_egaGC[5] & 0x08;
  m_egaReadShift = (m_egaGC[4] & 3) * 8;
  // caminho rapido: modo 0, sem set/reset, sem rotacao/operacao, mascara de bits FFh
  m_egaFast      = (m_egaWMode == 0 && (m_egaGC[1] & 0x0f) == 0 && m_egaGC[3] == 0 && m_egaGC[8] == 0xff);
}


// grava uma posicao (ja combinada com a mascara de planos) e atualiza a copia visivel
inline void Machine::egaStore(int offset, uint32_t value)
{
  m_egaVRAM[offset] = value;
  if (m_egaCompact) {
    // largura logica (CRTC 13h) diferente da visivel, ex.: Lemmings (44 bytes por linha):
    // a copia guarda so a parte visivel de cada linha (m_egaW posicoes)
    for (int k = 0; k < m_egaSlots; ++k) {
      unsigned rel = (offset - m_egaSlotStart[k]) & EGA_PLANEMASK;
      if (rel < m_egaSpan) {
        unsigned line = rel / m_egaP;
        unsigned col  = rel - line * m_egaP;
        if (col < m_egaW) {
          if (!m_egaHalf)
            m_egaSlot[k][line * m_egaW + col] = value;
          else if ((line & 1) == 0)    // modo 10h: so as linhas pares
            m_egaSlot[k][(line >> 1) * m_egaW + col] = value;
        }
      }
    }
    return;
  }
  // as copias (uma por pagina guardada) que cobrem este endereco tambem sao atualizadas
  unsigned rel = (offset - m_egaSlotStart[0]) & EGA_PLANEMASK;
  if (rel < m_egaShadowSize)
    m_egaSlot[0][rel] = value;
  if (m_egaSlots > 1) {
    rel = (offset - m_egaSlotStart[1]) & EGA_PLANEMASK;
    if (rel < m_egaShadowSize)
      m_egaSlot[1][rel] = value;
  }
}


// largura logica da linha (CRTC 13h, em palavras) nos modos EGA
void Machine::egaSetPitch(int value)
{
  int p = value * 2;
  if (p < m_egaW)
    p = m_egaW;
  if (p == m_egaP)
    return;
  m_egaP = p;
  if (m_egaDirect) {
    m_graphicsAdapter.setEGAPitch(p);
    return;
  }
  m_egaCompact = m_egaHalf || (m_egaP != m_egaW);
  m_egaSpan    = m_egaLines * m_egaP;
  m_egaSlotStart[0] = m_egaSlotStart[1] = -1;   // recopia
  egaSetStart(m_egaStart);
}


// muda o endereco de inicio (troca de pagina / rolagem): recopia a pagina visivel
//   No modo 0Dh ha duas copias (uma por pagina): ao alternar entre duas paginas so troca o ponteiro
//   do video, sem copiar nada (sem "rasgos" na tela). Uma pagina nova e copiada na copia que NAO esta
//   na tela e so depois passa a ser mostrada.
void Machine::egaSetStart(int start)
{
  start &= EGA_PLANEMASK;
  m_egaStart = start;
  if (m_egaDirect) {
    // modo 10h (640x350): o video le direto da memoria EGA (PSRAM), sem copia
    m_graphicsAdapter.setEGAShadow(m_egaVRAM + start);
    return;
  }
  for (int k = 0; k < m_egaSlots; ++k)
    if (m_egaSlotStart[k] == start) {
      m_egaShown = k;
      m_graphicsAdapter.setEGAShadow(m_egaSlot[k]);
      return;
    }
  int k = (m_egaSlots > 1) ? (m_egaShown ^ 1) : 0;
  uint32_t * dst = m_egaSlot[k];
  if (m_egaCompact) {
    int step  = m_egaHalf ? 2 : 1;
    for (int y = 0; y < m_egaLines / step; ++y) {
      int src = (start + y * step * m_egaP) & EGA_PLANEMASK;
      if (src + m_egaW <= EGA_PLANESIZE)
        memcpy(dst + y * m_egaW, m_egaVRAM + src, m_egaW * 4);
      else
        for (int x = 0; x < m_egaW; ++x)
          dst[y * m_egaW + x] = m_egaVRAM[(src + x) & EGA_PLANEMASK];
    }
  } else {
    int size  = m_egaShadowSize;
    int first = EGA_PLANESIZE - start;
    if (first >= size)
      memcpy(dst, m_egaVRAM + start, size * 4);
    else {
      memcpy(dst, m_egaVRAM + start, first * 4);
      memcpy(dst + first, m_egaVRAM, (size - first) * 4);
    }
  }
  m_egaSlotStart[k] = start;
  m_egaShown = k;
  m_graphicsAdapter.setEGAShadow(dst);
}


void IRAM_ATTR Machine::egaWrite(int offset, uint8_t value)
{
  offset &= EGA_PLANEMASK;
  uint32_t old = m_egaVRAM[offset];
  uint32_t mm  = m_egaMapMask32;

  if (m_egaFast) {
    egaStore(offset, (old & ~mm) | (value * 0x01010101u & mm));
    return;
  }

  uint32_t latch = m_egaLatch32;
  uint32_t d;
  switch (m_egaWMode) {
    case 1:
      // modo 1: copia os latches
      egaStore(offset, (old & ~mm) | (latch & mm));
      return;
    case 2:
      // modo 2: bits 0-3 do valor viram a cor de cada plano
      d = egaExpand4(value & 0x0f);
      break;
    default:
    {
      // modo 0: rotacao + set/reset
      uint8_t v = m_egaRot ? (uint8_t)((value >> m_egaRot) | (value << (8 - m_egaRot))) : value;
      d = (v * 0x01010101u & ~m_egaESR32) | (m_egaSR32 & m_egaESR32);
      break;
    }
  }
  switch (m_egaFunc) {
    case 1: d &= latch; break;
    case 2: d |= latch; break;
    case 3: d ^= latch; break;
  }
  d = (d & m_egaBM32) | (latch & ~m_egaBM32);
  egaStore(offset, (old & ~mm) | (d & mm));
}


uint8_t IRAM_ATTR Machine::egaRead(int offset)
{
  uint32_t v = m_egaVRAM[offset & EGA_PLANEMASK];
  m_egaLatch32 = v;
  if (m_egaReadMode1) {
    // modo de leitura 1: comparacao de cor (bit = 1 onde todos os planos "importam" e batem)
    uint32_t x = ~(v ^ m_egaCC32) | ~m_egaDC32;
    return (uint8_t)(x & (x >> 8) & (x >> 16) & (x >> 24));
  }
  return (uint8_t)(v >> m_egaReadShift);
}


// pixel pela BIOS (INT 10h AH=0Ch/0Dh), modo 0Dh: 40 bytes por linha, pagina de 8KB
void Machine::egaPutPixel(int page, int x, int y, uint8_t color)
{
  int off = (page * m_egaPageSize + y * m_egaBytesPerLine + (x >> 3)) & EGA_PLANEMASK;
  uint32_t bit = (0x80 >> (x & 7)) * 0x01010101u;
  uint32_t c   = egaExpand4(color & 0x0f) & bit;
  uint32_t v   = m_egaVRAM[off];
  v = (color & 0x80) ? (v ^ c) : ((v & ~bit) | c);
  egaStore(off, v);
}


uint8_t Machine::egaGetPixel(int page, int x, int y)
{
  int off = (page * m_egaPageSize + y * m_egaBytesPerLine + (x >> 3)) & EGA_PLANEMASK;
  uint8_t  bit = 0x80 >> (x & 7);
  uint32_t v   = m_egaVRAM[off];
  uint8_t color = 0;
  for (int p = 0; p < 4; ++p)
    if ((v >> (p * 8)) & bit)
      color |= 1 << p;
  return color;
}


// paleta EGA (INT 10h AH=10h)
void Machine::egaSetPaletteReg(int index, uint8_t value)
{
  if (!m_egaVRAM)
    return;
  index &= 0x1f;
  m_egaAttr[index] = value;
  if (index < 16)
    m_graphicsAdapter.setEGAPaletteItem(index, value);
}


// liga o modo 0Dh (320x200) ou 0Eh (640x200) (INT 10h AH=00h). clear = false quando o bit 7 de AL esta ligado
bool Machine::setEGAMode(int mode, bool clear)
{
  bool m350 = (mode == 0x10);
  if (!m_egaVRAM || (mode != 0x0d && mode != 0x0e && !(m350 && m_videoCard == 3)))
    return false;

  bool hires = (mode == 0x0e) || m350;
  m_egaMode         = mode;
  m_egaDirect       = false;
  m_egaHalf         = m350;   // 10h: copia com as linhas pares (175 x 80 posicoes = 56KB)
  m_egaW            = hires ? 80 : 40;
  m_egaP            = m_egaW;
  m_egaLines        = m350 ? 350 : 200;
  m_egaCompact      = m350;
  m_egaSpan         = m_egaLines * m_egaP;
  m_egaCharH        = m350 ? 14 : 8;
  m_egaBytesPerLine = hires ? 80 : 40;
  m_egaPageSize     = m350 ? 0x8000 : (hires ? 0x4000 : 0x2000);
  // copia visivel: 0Dh = 8K posicoes (32KB, em B0000-B7FFF); 0Eh = 16K posicoes (64KB, toda a B0000-BFFFF)
  m_egaShadowSize   = hires ? 16384 : 8192;

  // registradores no estado do modo
  memset(m_egaSeq, 0, sizeof(m_egaSeq));
  memset(m_egaGC, 0, sizeof(m_egaGC));
  m_egaSeq[0] = 0x03;
  m_egaSeq[1] = hires ? 0x01 : 0x09;
  m_egaSeq[2] = 0x0f;
  m_egaSeq[4] = 0x06;
  m_egaGC[5]  = 0x00;
  m_egaGC[6]  = 0x05;
  m_egaGC[7]  = 0x0f;
  m_egaGC[8]  = 0xff;
  egaUpdateCache();
  m_egaAttrFlip = false;
  for (int i = 0; i < 16; ++i)
    egaSetPaletteReg(i, (i & 7) | ((i & 8) << 1));
  m_egaAttr[0x10] = 0x01;
  m_egaAttr[0x12] = 0x0f;

  if (clear)
    memset(m_egaVRAM, 0, EGA_PLANESIZE * 4);

  // copias da pagina visivel: 0Dh = 2 x 32KB (toda a B0000-BFFFF); 0Eh = 1 x 64KB
  m_egaSlots       = hires ? 1 : 2;
  m_egaSlot[0]     = (uint32_t*)s_videoMemory;
  m_egaSlot[1]     = (uint32_t*)(s_videoMemory + 32768);
  m_egaSlotStart[0] = m_egaSlotStart[1] = -1;
  m_egaShown       = 0;
  m_egaStartPending = 0;

  m_CGA6845[0x0c] = m_CGA6845[0x0d] = 0;
  m_graphicsAdapter.setEGAPitch(m_egaW);
  egaSetStart(0);
  m_egaActive = true;
  m_graphicsAdapter.setEmulation(m350 ? GraphicsAdapter::Emulation::PC_Graphics_EGA_640x350_16Colors :
                                 hires ? GraphicsAdapter::Emulation::PC_Graphics_EGA_640x200_16Colors : GraphicsAdapter::Emulation::PC_Graphics_EGA_320x200_16Colors);
  m_graphicsAdapter.enableVideo(true);

  // area de dados da BIOS
  uint8_t * bda = s_memory + 0x400;
  bda[0x49] = mode;                         // modo de video
  *(uint16_t*)(bda + 0x4a) = hires ? 80 : 40;  // colunas
  *(uint16_t*)(bda + 0x4c) = m_egaPageSize; // tamanho da pagina
  *(uint16_t*)(bda + 0x4e) = 0;             // inicio da pagina
  memset(bda + 0x50, 0, 16);                // posicoes do cursor
  bda[0x62] = 0;                            // pagina ativa
  *(uint16_t*)(bda + 0x63) = 0x3d4;         // porta do CRTC
  bda[0x84] = 24;                           // linhas - 1
  *(uint16_t*)(bda + 0x85) = m_egaCharH;    // altura do caractere
  bda[0x87] = 0x60;                         // EGA ativa, 256KB
  bda[0x88] = m_videoCard == 3 ? 0x09 : 0x07;  // chaves: monitor de 350 linhas (EGA 640x350) ou de 200 linhas

  return true;
}


// ---- texto pela BIOS nos modos EGA (INT 10h AH=06h/07h/09h/0Ah/0Eh) ----


// desenha um caractere 8x8 na celula (row, col) da pagina. color bit 7 = XOR
void Machine::egaDrawChar(int page, int row, int col, uint8_t ch, uint8_t color)
{
  int h = m_egaCharH;
  auto glyph = (h == 14 ? fabgl::FONT_8x14.data : fabgl::FONT_8x8.data) + ch * h;
  uint32_t fg = egaExpand4(color & 0x0f);
  int base = page * m_egaPageSize + row * h * m_egaBytesPerLine + col;
  for (int y = 0; y < h; ++y) {
    int off = (base + y * m_egaBytesPerLine) & EGA_PLANEMASK;
    uint32_t g = glyph[y] * 0x01010101u;
    uint32_t v = (color & 0x80) ? (m_egaVRAM[off] ^ (fg & g)) : (fg & g);
    egaStore(off, v);
  }
}


// rola (ou limpa, lines = 0) uma janela de texto; up = true rola para cima
void Machine::egaScroll(int page, bool up, int lines, int r1, int c1, int r2, int c2, uint8_t color)
{
  int cols = m_egaBytesPerLine;
  if (c2 >= cols) c2 = cols - 1;
  if (r2 > 24) r2 = 24;
  if (r1 > r2 || c1 > c2)
    return;
  int height = r2 - r1 + 1;
  if (lines == 0 || lines > height)
    lines = height;
  int w    = c2 - c1 + 1;
  int base = page * m_egaPageSize;
  uint32_t fill = egaExpand4(color & 0x0f);
  // em linhas de pixels
  int h = m_egaCharH;
  int top = r1 * h, bottom = r2 * h + h - 1, shift = lines * h;
  if (up) {
    for (int y = top; y <= bottom; ++y) {
      int dst = base + y * cols + c1;
      int src = y + shift <= bottom ? base + (y + shift) * cols + c1 : -1;
      for (int x = 0; x < w; ++x)
        egaStore((dst + x) & EGA_PLANEMASK, src >= 0 ? m_egaVRAM[(src + x) & EGA_PLANEMASK] : fill);
    }
  } else {
    for (int y = bottom; y >= top; --y) {
      int dst = base + y * cols + c1;
      int src = y - shift >= top ? base + (y - shift) * cols + c1 : -1;
      for (int x = 0; x < w; ++x)
        egaStore((dst + x) & EGA_PLANEMASK, src >= 0 ? m_egaVRAM[(src + x) & EGA_PLANEMASK] : fill);
    }
  }
}


// teletipo (AH=0Eh): escreve na posicao do cursor da pagina e avanca, rolando a tela se precisar
void Machine::egaTTY(int page, uint8_t ch, uint8_t color)
{
  uint8_t * bda = s_memory + 0x400;
  page &= 7;
  int cols = m_egaBytesPerLine;
  int col = bda[0x50 + page * 2], row = bda[0x51 + page * 2];
  switch (ch) {
    case 0x07:                        // BEL: ignorado
      break;
    case 0x08:                        // BS
      if (col > 0) --col;
      break;
    case 0x0a:                        // LF
      ++row;
      break;
    case 0x0d:                        // CR
      col = 0;
      break;
    default:
      egaDrawChar(page, row, col, ch, color);
      if (++col >= cols) {
        col = 0;
        ++row;
      }
      break;
  }
  if (row > 24) {
    egaScroll(page, true, 1, 0, 0, 24, cols - 1, 0);
    row = 24;
  }
  bda[0x50 + page * 2] = col;
  bda[0x51 + page * 2] = row;
}


// AH=09h/0Ah: escreve o caractere 'count' vezes a partir do cursor, sem mover o cursor
void Machine::egaWriteChars(int page, uint8_t ch, uint8_t color, int count)
{
  uint8_t * bda = s_memory + 0x400;
  page &= 7;
  int cols = m_egaBytesPerLine;
  int col = bda[0x50 + page * 2], row = bda[0x51 + page * 2];
  while (count-- > 0 && row <= 24) {
    egaDrawChar(page, row, col, ch, color);
    if (++col >= cols) {
      col = 0;
      ++row;
    }
  }
}


bool Machine::interrupt(void * context, int num)
{
  auto m = (Machine*)context;


  // emu interrupts callable only inside the BIOS segment
  if (i8086::CS() == BIOS_SEG) {
    switch (num) {

      // Put Char for debug (AL)
      case 0xf4:
        printf("%c", i8086::AX() & 0xff);
        return true;

      // BIOS helpers (AH = select helper function)
      case 0xf5:
        m->m_BIOS.helpersEntry();
        return true;

      // set or reset flag CF before IRET, replacing value in stack with current value
      case 0xf6:
      {
        auto savedFlags = (uint16_t*) (s_memory + i8086::SS() * 16 + (uint16_t)(i8086::SP() + 4));
        *savedFlags = (*savedFlags & 0xfffe) | i8086::flagCF();
        return true;
      }

      // set or reset flag ZF before IRET, replacing value in stack with current value
      case 0xf7:
      {
        auto savedFlags = (uint16_t*) (s_memory + i8086::SS() * 16 + (uint16_t)(i8086::SP() + 4));
        *savedFlags = (*savedFlags & 0xffbf) | (i8086::flagZF() << 6);
        return true;
      }

      // set or reset flag IF before IRET, replacing value in stack with current value
      case 0xf8:
      {
        auto savedFlags = (uint16_t*) (s_memory + i8086::SS() * 16 + (uint16_t)(i8086::SP() + 4));
        *savedFlags = (*savedFlags & 0xfdff) | (i8086::flagIF() << 9);
        return true;
      }

      // test point P0
      case 0xf9:
        printf("P0 AX=%04X BX=%04X CX=%04X DX=%04X DS=%04X\n", i8086::AX(), i8086::BX(), i8086::CX(), i8086::DX(), i8086::DS());
        return true;

      // test point P1
      case 0xfa:
        printf("P1 AX=%04X BX=%04X CX=%04X DX=%04X DS=%04X\n", i8086::AX(), i8086::BX(), i8086::CX(), i8086::DX(), i8086::DS());
        return true;

      // BIOS disk handler (INT 13h)
      case 0xfb:
        m->m_BIOS.diskHandlerEntry();
        return true;

      // BIOS video handler (INT 10h)
      case 0xfc:
        m->m_BIOS.videoHandlerEntry();
        return true;

    }
  }

  // not hanlded
  return false;
}


void Machine::speakerSetFreq()
{
  //printf("speakerSetFreq: count = %d\n", m_PIT8253.timerInfo(2).resetCount);
  int timerCount = m_PIT8253.timerInfo(2).resetCount;
  if (timerCount == 0)
    timerCount = 65536;
  int freq = PIT_TICK_FREQ / timerCount;
  //printf("   freq = %dHz\n", freq);
  // XPC: frequencias acima de ~8KHz nao cabem no gerador de som (16KHz de amostragem): elas "dobram"
  //      e viram zumbido audivel. No PC real sao inaudiveis (ex.: Digger usa 30KHz como silencio).
  m_speakerAudible = (freq <= 8000);
  if (m_speakerAudible)
    m_sinWaveGen.setFrequency(freq);
  speakerEnableDisable();
}


// XPC: silencia o som enquanto o menu esta aberto (a maquina emulada fica parada)
void Machine::pauseSound(bool value)
{
  m_soundPaused = value;
  if (value)
    m_bitbangGen.enable(false);
  if (m_adlibOn)
    m_adlib.pause(value);
  speakerEnableDisable();
}


// XPC: cada subida do bit 1 (com o timer 2 parado) e um pulso; a frequencia vem do intervalo entre pulsos
void Machine::setSpeakerMode(int v)
{
  if (v != 2 && !m_pcmGen.begin())
    v = 2;
  m_spkMode   = v;
  m_pcmActive = (v == 1);
  m_bitbangGen.enable(false);
  m_bitbangFreq = 0;
  m_pcmGen.setMute(!m_pcmActive);
  m_pcmGen.enable(v != 2);
}


// horario completo de um evento (us): o relogio atual menos a diferenca em 16 bits (eventos tem menos de 65ms)
#define EVT(i) (now - (int64_t)(uint16_t)((uint16_t)now - (m_ev[i] & 0xfffe)))

// IMPORTANTE: o FabGL chama getSample() dentro de uma interrupcao, onde o ESP32 nao permite ponto flutuante
//             (usar float/double aqui reinicia a placa sem mensagem). Tudo em inteiros: tempo em 1/256 us.
int IRAM_ATTR SpeakerPCMGenerator::getSample()
{
  if (!m_ev)
    return 0;
  constexpr int64_t DT      = 1000000LL * 256 / FABGL_SOUNDGEN_DEFAULT_SAMPLE_RATE;   // 15625 (61us)
  constexpr int64_t LATENCY = 20000LL * 256;    // atraso fixo: os eventos ja chegaram quando a amostra e feita
  constexpr int64_t SYNC    = 15000LL * 256;
  constexpr int64_t MINP    = 120LL * 256;      // pulso isolado minimo
  constexpr int64_t ISOL    = 400LL * 256;
  int64_t now = esp_timer_get_time();
  int64_t target = now * 256 - LATENCY;
  // fora de sincronia (inicio, pausa, atraso do sistema): realinha e descarta eventos velhos
  if (m_play < target - SYNC || m_play > target + SYNC) {
    m_play = target;
    while (m_tail != m_head && EVT(m_tail) * 256 < m_play) {
      m_level = m_ev[m_tail] & 1;
      m_tail = (m_tail + 1) & (RING - 1);
    }
    m_riseT = m_play;
    m_credit = 0;
  }
  int64_t t1 = m_play + DT;
  int64_t pos = m_play;
  while (m_tail != m_head) {
    int64_t et = EVT(m_tail) * 256;
    if (et >= t1)
      break;
    if (et < pos)
      et = pos;
    bool lv = m_ev[m_tail] & 1;
    if (m_level)
      m_credit += et - pos;
    if (lv && !m_level) {                 // subida
      m_lastRise = m_riseT;
      m_riseT = et;
    } else if (!lv && m_level) {          // descida: pulso curto e isolado ganha largura minima
      int64_t len = et - m_riseT;
      if (len < MINP && m_riseT - m_lastRise > ISOL)
        m_credit += MINP - len;
    }
    m_level = lv;
    pos = et;
    m_tail = (m_tail + 1) & (RING - 1);
  }
  if (m_level)
    m_credit += t1 - pos;
  int64_t hi = m_credit < DT ? m_credit : DT;
  m_credit -= hi;
  if (m_credit > 4 * DT)
    m_credit = 4 * DT;
  m_play = t1;
  if (m_mute) {
    m_xPrev = m_yPrev = 0;
    return 0;
  }

  // fracao ligada (0..4096) -> amostra, sem nivel DC (passa-altas: y = x - x' + y' * 255/256)
  int32_t x = (int32_t)(hi * 4096 / DT);
  int32_t y = x - m_xPrev + (m_yPrev * 255) / 256;
  m_xPrev = x;
  m_yPrev = y;
  int v = (y * 200 / 4096) * volume() / 100;
  return v > 127 ? 127 : (v < -128 ? -128 : v);
}


void Machine::bitbangSpeaker(bool level)
{
  // PCM / Auto: todo pulso vai para o gerador por amostras
  if (m_spkMode != 2 && level != m_bitbangLevel && !m_soundPaused) {
    m_pcmGen.push(level);
    // Auto: mais de ~5000 pulsos/s = som digitalizado (PCM); menos = notas (Classic, mais estavel)
    if (m_spkMode == 0 && level) {
      int64_t now = esp_timer_get_time();
      ++m_rateRises;
      int64_t el = now - m_rateStart;
      if (el >= 20000) {
        bool pcm = m_rateRises * 1000000LL / el > 5000;
        if (pcm != m_pcmActive) {
          m_pcmActive = pcm;
          m_pcmGen.setMute(!pcm);
          if (pcm) {
            m_bitbangGen.enable(false);
            m_bitbangFreq = 0;
          }
        }
        m_rateStart = now;
        m_rateRises = 0;
      }
    }
  }
  if (m_pcmActive) {
    if (level && !m_bitbangLevel && !m_soundPaused)
      ++m_bbPulses;
    m_bitbangLevel = level;
    return;
  }
  if (level && !m_bitbangLevel && !m_soundPaused) {
    // Digger: um pulso curtissimo a cada interrupcao do timer 0; a nota e a propria taxa do timer 0
    int64_t now = esp_timer_get_time();
    int64_t dt  = now - m_bitbangLastRise;
    m_bitbangLastRise = now;
    int cnt = m_PIT8253.timerInfo(0).resetCount;
    if (cnt == 0) cnt = 65536;
    int freq0 = PIT_TICK_FREQ / cnt;
    int freq  = 0;
    if (dt > 250 && dt < 25000) {
      // media do intervalo entre pulsos: absorve o tremor (pulsos que caem em ticks alternados, IRQs atrasados)
      // mudanca grande (> 30%) = nota nova: recomeca a media
      if (m_bbAvgDt <= 0 || dt > m_bbAvgDt * 13 / 10 || dt < m_bbAvgDt * 7 / 10)
        m_bbAvgDt = dt;
      else
        m_bbAvgDt = (m_bbAvgDt * 3 + dt) / 4;
      freq = 1000000 / (int)m_bbAvgDt;
      // um pulso por tick do timer 0 (Digger): usa a taxa exata do timer
      if (freq0 >= 40 && freq0 <= 4000 && abs(freq - freq0) * 10 < freq0)
        freq = freq0;
    } else if (freq0 >= 40 && freq0 <= 4000) {
      m_bbAvgDt = 0;
      freq = freq0;                   // primeiro pulso de uma nota
    }
    ++m_bbPulses;
    m_bbRawFreq = freq;
    if (freq >= 40 && freq <= 4000) {
      if (freq != m_bitbangFreq) {
        m_bitbangFreq = freq;
        m_bitbangGen.setFrequency(freq);
      }
      m_bitbangGen.enable(true);
    }
  }
  m_bitbangLevel = level;
}


// sem pulsos ha mais de 50ms: silencio
void Machine::bitbangCheckIdle()
{
  if (m_bitbangFreq && esp_timer_get_time() - m_bitbangLastRise > 50000) {
    m_bitbangGen.enable(false);
    m_bitbangFreq = 0;
  }
}


void Machine::speakerEnableDisable()
{
  bool genEnabled = m_PIT8253.getGate(2);
  if (genEnabled && m_speakerDataEnable && !m_soundPaused && m_speakerAudible) {
    // speaker enabled
    m_sinWaveGen.enable(true);
  } else {
    // speaker disabled
    m_sinWaveGen.enable(false);
  }
}


void Machine::dumpMemory(char const * filename)
{
  constexpr int BLOCKLEN = 1024;
  auto file = FileBrowser(m_baseDir).openFile(filename, "wb");
  if (file) {
    for (int i = 0; i < 1048576; i += BLOCKLEN)
      fwrite(s_memory + i, 1, BLOCKLEN, file);
    fclose(file);
  }
}


void Machine::dumpInfo(char const * filename)
{
  auto file = FileBrowser(m_baseDir).openFile(filename, "wb");
  if (file) {
    // CPU state
    fprintf(file, " CS   DS   ES   SS\n");
    fprintf(file, "%04X %04X %04X %04X\n\n", i8086::CS(), i8086::DS(), i8086::ES(), i8086::SS());
    fprintf(file, " IP   AX   BX   CX   DX   SI   DI   BP   SP\n");
    fprintf(file, "%04X %04X %04X %04X %04X %04X %04X %04X %04X\n\n", i8086::IP(), i8086::AX(), i8086::BX(), i8086::CX(), i8086::DX(), i8086::SI(), i8086::DI(), i8086::BP(), i8086::SP());
    fprintf(file, "O D I T S Z A P C\n");
    fprintf(file, "%d %d %d %d %d %d %d %d %d\n\n", i8086::flagOF(), i8086::flagDF(), i8086::flagIF(), i8086::flagTF(), i8086::flagSF(), i8086::flagZF(), i8086::flagAF(), i8086::flagPF(), i8086::flagCF());
    fprintf(file, "CS+IP: %05X\n", i8086::CS() * 16 + i8086::IP());
    fprintf(file, "SS+SP: %05X\n\n", i8086::SS() * 16 + i8086::SP());
    fclose(file);
  }
}
