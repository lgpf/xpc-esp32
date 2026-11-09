// XPC Cobra - AdLib (YM3812/OPL2) para o PCEmulator FabGL
// Usa o nucleo fmopl do MAME (GPL-2.0+)
//
// O som e gerado por uma task no core do video. O 8086 emulado nunca espera por ela:
// as escritas nos registradores vao para uma fila (sem trava) que a task aplica
// antes de cada bloco de amostras.

#pragma once

#include "fabgl.h"

using fabgl::WaveformGenerator;

class AdLib : public WaveformGenerator {
public:
  AdLib();
  ~AdLib();

  // WaveformGenerator
  void setFrequency(int value) { }
  int getSample();

  // portas 0x388 (indice/status) e 0x389 (dados)
  void    write(int port, uint8_t value);
  uint8_t read(int port);

  void begin(int sampleRate);
  void reset();
  void pause(bool value)  { m_paused = value; }

private:
  void checkTimers();
  static void renderTask(void * arg);

  void *    m_chip;
  uint8_t   m_index;
  uint8_t   m_timerVal[2];
  bool      m_timerOn[2];
  int64_t   m_timerStart[2];
  uint8_t   m_status;
  uint8_t   m_timerMask;

  // fila de escritas: 8086 (produtor) -> task de som (consumidor)
  static constexpr int WQSIZE = 2048;    // potencia de 2
  uint16_t          m_wq[WQSIZE];        // (registrador << 8) | valor
  volatile uint32_t m_wqHead;
  volatile uint32_t m_wqTail;
  volatile bool     m_resetRequest;

  // buffer circular de amostras geradas
  static constexpr int RINGSIZE = 1024;  // potencia de 2
  int8_t            m_ring[RINGSIZE];
  volatile uint32_t m_ringHead;          // escrito pela task de som
  volatile uint32_t m_ringTail;          // lido pelo gerador de som (ISR)

  TaskHandle_t      m_task;
  volatile bool     m_paused;
};
