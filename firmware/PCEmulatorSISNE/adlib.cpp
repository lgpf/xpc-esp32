// XPC Cobra - AdLib (YM3812/OPL2) para o PCEmulator FabGL

#include "adlib.h"
#include "fmopl.h"

#include "esp_timer.h"


#define ADLIB_CLOCK  3579545


static device_t * s_dummyDevice = nullptr;


AdLib::AdLib()
  : m_chip(nullptr),
    m_index(0),
    m_timerVal(),
    m_timerOn(),
    m_timerStart(),
    m_status(0),
    m_timerMask(0),
    m_wqHead(0),
    m_wqTail(0),
    m_resetRequest(false),
    m_ringHead(0),
    m_ringTail(0),
    m_task(nullptr),
    m_paused(false)
{
}


AdLib::~AdLib()
{
  if (m_task)
    vTaskDelete(m_task);
  if (m_chip)
    ym3812_shutdown(m_chip);
}


void AdLib::reset()
{
  m_timerOn[0] = m_timerOn[1] = false;
  m_status    = 0;
  m_timerMask = 0;
  m_wqTail    = m_wqHead;    // descarta escritas pendentes
  m_resetRequest = true;     // a task de som reinicia o chip
}


// timers e registrador de status do OPL2 (usados pelos jogos para detectar a AdLib)
//   timer 1: 80us por passo, timer 2: 320us por passo
void AdLib::checkTimers()
{
  int64_t now = esp_timer_get_time();
  for (int t = 0; t < 2; ++t) {
    if (m_timerOn[t]) {
      int64_t period = (int64_t)(256 - m_timerVal[t]) * (t == 0 ? 80 : 320);
      if (now - m_timerStart[t] >= period) {
        uint8_t flag = (t == 0 ? 0x40 : 0x20);
        if ((m_timerMask & flag) == 0)
          m_status |= flag | 0x80;
        m_timerStart[t] = now;
      }
    }
  }
}


void AdLib::write(int port, uint8_t value)
{
  if ((port & 1) == 0) {
    m_index = value;
    return;
  }

  switch (m_index) {
    case 0x02:
      m_timerVal[0] = value;
      break;
    case 0x03:
      m_timerVal[1] = value;
      break;
    case 0x04:
      if (value & 0x80) {
        m_status = 0;     // reset dos flags de IRQ
      } else {
        int64_t now = esp_timer_get_time();
        m_timerMask = value & 0x60;
        if ((value & 0x01) && !m_timerOn[0])
          m_timerStart[0] = now;
        if ((value & 0x02) && !m_timerOn[1])
          m_timerStart[1] = now;
        m_timerOn[0] = value & 0x01;
        m_timerOn[1] = value & 0x02;
      }
      return;   // timers sao tratados aqui, nao precisam ir ao chip
  }

  // enfileira para a task de som (se a fila estiver cheia, descarta)
  uint32_t head = m_wqHead;
  if (head - m_wqTail < WQSIZE) {
    m_wq[head & (WQSIZE - 1)] = (m_index << 8) | value;
    m_wqHead = head + 1;
  }
}


uint8_t AdLib::read(int port)
{
  if ((port & 1) == 0) {
    checkTimers();
    return m_status | 0x06;
  }
  return 0xff;
}


// cria o chip OPL2 e a task que gera o som no core do video (o core da CPU emulada fica livre)
void AdLib::begin(int sampleRate)
{
  if (!m_chip) {
    if (!s_dummyDevice)
      s_dummyDevice = new device_t(machine_config(), 0, "adlib", nullptr, ADLIB_CLOCK);
    m_chip = ym3812_init(s_dummyDevice, ADLIB_CLOCK, sampleRate);
  }
  if (m_chip && !m_task) {
    int core = fabgl::CoreUsage::busiestCore();
    xTaskCreatePinnedToCore(renderTask, "adlib", 3072, this, 1, &m_task, core < 0 ? 0 : core);
  }
}


void AdLib::renderTask(void * arg)
{
  auto a = (AdLib *) arg;
  constexpr int BLOCK = 32;    // 2ms a 16KHz
  OPLSAMPLE buf[BLOCK];
  int burst = 0;
  while (true) {

    if (a->m_resetRequest) {
      a->m_resetRequest = false;
      ym3812_reset_chip(a->m_chip);
    }

    // aplica as escritas pendentes
    uint32_t tail = a->m_wqTail;
    uint32_t head = a->m_wqHead;
    while (tail != head) {
      uint16_t v = a->m_wq[tail & (WQSIZE - 1)];
      ym3812_write(a->m_chip, 0, v >> 8);
      ym3812_write(a->m_chip, 1, v & 0xff);
      ++tail;
    }
    a->m_wqTail = tail;

    uint32_t used = a->m_ringHead - a->m_ringTail;
    if (a->m_paused) {
      vTaskDelay(10);
    } else if (RINGSIZE - used >= BLOCK) {
      ym3812_update_one(a->m_chip, buf, BLOCK);
      uint32_t rh = a->m_ringHead;
      for (int i = 0; i < BLOCK; ++i) {
        int sample = buf[i] >> 6;
        if (sample > 127)
          sample = 127;
        else if (sample < -128)
          sample = -128;
        a->m_ring[(rh + i) & (RINGSIZE - 1)] = sample;
      }
      a->m_ringHead = rh + BLOCK;
      // XPC: nunca fica presa gerando som. Se o chip nao da conta (ex.: Arkanoid II), a task rodava
      //      sem parar e as outras tasks deste core (teclado, etc.) morriam de fome: a placa "travava".
      if (++burst >= 4) {
        burst = 0;
        vTaskDelay(1);
      }
    } else {
      burst = 0;
      vTaskDelay(1);
    }
  }
}


int AdLib::getSample()
{
  if (m_paused) {
    m_ringTail = m_ringHead;   // descarta o que estava no buffer
    return 0;
  }
  if (m_ringHead == m_ringTail)
    return 0;   // buffer vazio
  int sample = m_ring[m_ringTail & (RINGSIZE - 1)];
  m_ringTail = m_ringTail + 1;
  return sample * volume() / 127;
}
