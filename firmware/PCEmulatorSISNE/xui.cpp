// XPC Cobra - interface em modo texto 80x25 (estilo CheckIt / Norton)

#include "xui.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>


using fabgl::PS2Controller;
using fabgl::VirtualKey;
using fabgl::VirtualKeyItem;
using xpc::GraphicsAdapter;


namespace xui {


static constexpr int COLS = 80;
static constexpr int ROWS = 25;
static constexpr int SCREENSIZE = COLS * ROWS * 2;


static GraphicsAdapter * s_adapter = nullptr;
static uint8_t *         s_screen  = nullptr;   // buffer do menu (char + atributo)
static bool              s_open    = false;

// estado salvo do video do programa emulado
static GraphicsAdapter::Emulation s_savedEmulation;
static uint8_t const *            s_savedBuffer;
static bool                       s_savedBlink;
static bool                       s_savedCursor;
static bool                       s_savedEnabled;


// caracteres CP437
enum : uint8_t {
  C_DTL = 0xC9, C_DTR = 0xBB, C_DBL = 0xC8, C_DBR = 0xBC, C_DH = 0xCD, C_DV = 0xBA,
  C_STL = 0xDA, C_STR = 0xBF, C_SBL = 0xC0, C_SBR = 0xD9, C_SH = 0xC4, C_SV = 0xB3,
  C_TL  = 0xC7, C_TR  = 0xB6,      // juncao dupla/simples para separadores
  C_LIGHT = 0xB0, C_MEDIUM = 0xB1, C_FULL = 0xDB,
  C_UP = 0x18, C_DOWN = 0x19,
};


void begin(GraphicsAdapter * adapter)
{
  s_adapter = adapter;
  if (!s_screen)
    s_screen = (uint8_t *) heap_caps_malloc(SCREENSIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  clear(0x00, ' ');   // comeca preta (o boot desenha por cima)
}


void open()
{
  if (s_open || !s_adapter)
    return;
  s_open = true;

  s_savedEmulation = s_adapter->emulation();
  s_savedBuffer    = s_adapter->videoBuffer();
  s_savedBlink     = s_adapter->bit7Blink();
  s_savedCursor    = s_adapter->cursorVisible();
  s_savedEnabled   = s_adapter->isVideoEnabled();

  s_adapter->setCursorVisible(false);
  s_adapter->setBit7Blink(false);
  s_adapter->setVideoBuffer(s_screen);
  s_adapter->setEmulation(GraphicsAdapter::Emulation::PC_Text_80x25_16Colors);
  s_adapter->enableVideo(true);

  // teclado em modo "teclas virtuais" (o 8042 emulado nao recebe nada)
  auto kbd = PS2Controller::keyboard();
  if (kbd) {
    kbd->enableVirtualKeys(true, true);
    kbd->emptyVirtualKeyQueue();
  }
}


void close()
{
  if (!s_open)
    return;
  s_open = false;

  auto kbd = PS2Controller::keyboard();
  if (kbd)
    kbd->enableVirtualKeys(false, false);

  if (s_savedEmulation != GraphicsAdapter::Emulation::None) {
    s_adapter->setVideoBuffer(s_savedBuffer);
    s_adapter->setEmulation(s_savedEmulation);
    s_adapter->setBit7Blink(s_savedBlink);
    s_adapter->setCursorVisible(s_savedCursor);
    s_adapter->enableVideo(s_savedEnabled);
  }
}


bool isOpen()
{
  return s_open;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// desenho


void putChar(int x, int y, uint8_t ch, uint8_t attr)
{
  if (x < 0 || y < 0 || x >= COLS || y >= ROWS)
    return;
  auto p = s_screen + (y * COLS + x) * 2;
  p[0] = ch;
  p[1] = attr;
}


void setAttr(int x, int y, int w, uint8_t attr)
{
  for (int i = 0; i < w; ++i)
    if (x + i >= 0 && x + i < COLS && y >= 0 && y < ROWS)
      s_screen[(y * COLS + x + i) * 2 + 1] = attr;
}


void print(int x, int y, char const * text, uint8_t attr, int maxw)
{
  for (int i = 0; text[i] && i < maxw; ++i)
    putChar(x + i, y, (uint8_t) text[i], attr);
}


void printCenter(int y, char const * text, uint8_t attr)
{
  print((COLS - (int) strlen(text)) / 2, y, text, attr);
}


void fill(int x, int y, int w, int h, uint8_t ch, uint8_t attr)
{
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i)
      putChar(x + i, y + j, ch, attr);
}


void clear(uint8_t attr, uint8_t ch)
{
  fill(0, 0, COLS, ROWS, ch, attr);
}


void box(int x, int y, int w, int h, uint8_t frameAttr, uint8_t fillAttr, char const * title, uint8_t titleAttr, bool shadow)
{
  fill(x + 1, y + 1, w - 2, h - 2, ' ', fillAttr);
  putChar(x, y, C_DTL, frameAttr);
  putChar(x + w - 1, y, C_DTR, frameAttr);
  putChar(x, y + h - 1, C_DBL, frameAttr);
  putChar(x + w - 1, y + h - 1, C_DBR, frameAttr);
  for (int i = 1; i < w - 1; ++i) {
    putChar(x + i, y, C_DH, frameAttr);
    putChar(x + i, y + h - 1, C_DH, frameAttr);
  }
  for (int j = 1; j < h - 1; ++j) {
    putChar(x, y + j, C_DV, frameAttr);
    putChar(x + w - 1, y + j, C_DV, frameAttr);
  }
  if (title && *title) {
    int len = strlen(title);
    if (len > w - 6)
      len = w - 6;
    int tx = x + (w - len - 2) / 2;
    putChar(tx, y, ' ', titleAttr);
    print(tx + 1, y, title, titleAttr, len);
    putChar(tx + len + 1, y, ' ', titleAttr);
  }
  if (shadow) {
    // sombra: escurece o que esta a direita e embaixo
    for (int j = 1; j <= h; ++j)
      setAttr(x + w, y + j, 2, A_SHADOW);
    setAttr(x + 2, y + h, w, A_SHADOW);
  }
}


void helpBar(char const * text)
{
  fill(0, ROWS - 1, COLS, 1, ' ', A_BAR);
  int x = 1;
  bool hi = false;
  for (auto p = text; *p && x < COLS - 1; ++p) {
    if (*p == '~') {
      hi = !hi;
      continue;
    }
    putChar(x++, ROWS - 1, (uint8_t) *p, hi ? A_BARKEY : A_BAR);
  }
}


static char const * s_title = "Cobra XPC - V 3.02";

void setTitle(char const * title)
{
  s_title = title;
}


void desktop(char const * rightText)
{
  clear(A_DESKTOP, C_LIGHT);
  fill(0, 0, COLS, 1, ' ', A_BAR);
  print(1, 0, s_title, A_BAR);
  if (rightText) {
    int len = strlen(rightText);
    if (len > 60)
      len = 60;
    print(COLS - 1 - len, 0, rightText, A_BAR, len);
  }
  helpBar("");
}


uint8_t * saveScreen()
{
  auto saved = (uint8_t *) malloc(SCREENSIZE);
  if (saved)
    memcpy(saved, s_screen, SCREENSIZE);
  return saved;
}


void restoreScreen(uint8_t * saved)
{
  if (saved) {
    memcpy(s_screen, saved, SCREENSIZE);
    free(saved);
  }
}


static void setHelp(char const * text)
{
  helpBar(text);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// teclado


static bool s_refreshOnMonitor = false;
static bool s_monitorChanged   = false;
static int  s_lastSelected     = 0;

void setRefreshOnMonitor(bool value) { s_refreshOnMonitor = value; }
static void (*s_menuInfo)(int) = nullptr;
static int s_menuReserve = 0;
void setMenuReserve(int rows) { s_menuReserve = rows; }
void setMenuInfo(void (*callback)(int selected)) { s_menuInfo = callback; }
int lastSelected()                   { return s_lastSelected; }


void getKey(VirtualKeyItem * item)
{
  auto kbd = PS2Controller::keyboard();
  while (true) {
    if (kbd && kbd->getNextVirtualKey(item, -1) && item->down) {
      // CTRL + ALT + DOWN: ciclo do monitor tambem dentro do menu
      if ((item->vk == VirtualKey::VK_DOWN || item->vk == VirtualKey::VK_KP_DOWN) && item->CTRL && (item->LALT || item->RALT)) {
        if (s_adapter)
          s_adapter->cycleMonitorMode();
        if (s_refreshOnMonitor) {
          // o menu principal se redesenha para mostrar o novo modo do monitor
          s_monitorChanged = true;
          item->vk = VirtualKey::VK_NONE;
          return;
        }
        continue;
      }
      // CTRL + ALT + UP dentro do menu: ignora
      if ((item->vk == VirtualKey::VK_UP || item->vk == VirtualKey::VK_KP_UP) && item->CTRL && (item->LALT || item->RALT))
        continue;
      return;
    }
  }
}


static bool isEnter(VirtualKey vk)  { return vk == VirtualKey::VK_RETURN || vk == VirtualKey::VK_KP_ENTER; }
static bool isUp(VirtualKey vk)     { return vk == VirtualKey::VK_UP || vk == VirtualKey::VK_KP_UP; }
static bool isDown(VirtualKey vk)   { return vk == VirtualKey::VK_DOWN || vk == VirtualKey::VK_KP_DOWN; }
static bool isLeft(VirtualKey vk)   { return vk == VirtualKey::VK_LEFT || vk == VirtualKey::VK_KP_LEFT; }
static bool isRight(VirtualKey vk)  { return vk == VirtualKey::VK_RIGHT || vk == VirtualKey::VK_KP_RIGHT; }
static bool isHome(VirtualKey vk)   { return vk == VirtualKey::VK_HOME || vk == VirtualKey::VK_KP_HOME; }
static bool isEnd(VirtualKey vk)    { return vk == VirtualKey::VK_END || vk == VirtualKey::VK_KP_END; }
static bool isPgUp(VirtualKey vk)   { return vk == VirtualKey::VK_PAGEUP || vk == VirtualKey::VK_KP_PAGEUP; }
static bool isPgDn(VirtualKey vk)   { return vk == VirtualKey::VK_PAGEDOWN || vk == VirtualKey::VK_KP_PAGEDOWN; }


////////////////////////////////////////////////////////////////////////////////////////////////////
// menu


struct MenuItem {
  char const * label;
  int          labelLen;
  char const * value;
  int          valueLen;
  bool         separator;
};


// separa os itens (sem copiar: aponta para dentro de 'items')
static int parseItems(char const * items, MenuItem * out, int maxItems)
{
  int n = 0;
  char const * p = items;
  while (*p && n < maxItems) {
    char const * end = strchr(p, ';');
    if (!end)
      end = p + strlen(p);
    char const * tab = (char const *) memchr(p, '\t', end - p);
    MenuItem & it = out[n++];
    it.label     = p;
    it.labelLen  = (tab ? tab : end) - p;
    it.value     = tab ? tab + 1 : end;
    it.valueLen  = tab ? end - (tab + 1) : 0;
    it.separator = (it.labelLen == 1 && *p == '-');
    p = *end ? end + 1 : end;
  }
  return n;
}


int menuEx(char const * title, char const * items, int selected, int width, int * exitKey, char const * help)
{
  constexpr int MAXITEMS = 256;
  auto list = (MenuItem *) malloc(sizeof(MenuItem) * MAXITEMS);
  if (!list)
    return -1;
  int count = parseItems(items, list, MAXITEMS);
  if (count == 0) {
    free(list);
    return -1;
  }

  // largura: maior rotulo + maior valor
  int maxLabel = 0, maxValue = 0;
  for (int i = 0; i < count; ++i) {
    if (list[i].labelLen > maxLabel) maxLabel = list[i].labelLen;
    if (list[i].valueLen > maxValue) maxValue = list[i].valueLen;
  }
  int inner = maxLabel + (maxValue ? maxValue + 3 : 0) + 2;
  if (title && (int) strlen(title) + 4 > inner)
    inner = strlen(title) + 4;
  if (width > inner)
    inner = width;
  if (inner > COLS - 6)
    inner = COLS - 6;
  int w = inner + 2;

  const int MAXVISIBLE = ROWS - 5 - s_menuReserve;   // linhas 2..23 (menos o espaco reservado embaixo)
  int visible = count < MAXVISIBLE ? count : MAXVISIBLE;
  int h = visible + 2;
  int x = (COLS - w) / 2;
  int y = (ROWS - s_menuReserve - h) / 2;
  if (y < 2)
    y = 2;

  auto saved = saveScreen();
  setHelp(help ? help : "~\x18\x19~ Select   ~Enter~ OK   ~Esc~ Back");

  if (selected < 0 || selected >= count)
    selected = 0;
  while (list[selected].separator && selected < count - 1)
    ++selected;
  int top = 0;
  int result = -1;

  while (true) {
    // rolagem
    if (selected < top)
      top = selected;
    if (selected >= top + visible)
      top = selected - visible + 1;

    box(x, y, w, h, A_FRAME, A_WINDOW, title);
    for (int r = 0; r < visible; ++r) {
      int i = top + r;
      auto & it = list[i];
      int yy = y + 1 + r;
      if (it.separator) {
        putChar(x, yy, C_TL, A_FRAME);
        for (int k = 1; k < w - 1; ++k)
          putChar(x + k, yy, C_SH, A_FRAME);
        putChar(x + w - 1, yy, C_TR, A_FRAME);
        continue;
      }
      uint8_t attr = (i == selected) ? A_SELECTED : A_WINDOW;
      fill(x + 1, yy, inner, 1, ' ', attr);
      print(x + 2, yy, it.label, attr, it.labelLen < inner - 2 ? it.labelLen : inner - 2);
      if (it.valueLen) {
        int vx = x + 1 + inner - 1 - it.valueLen;
        print(vx, yy, it.value, i == selected ? A_SELECTED : A_VALUE, it.valueLen);
      }
      // item selecionado sempre marcado entre < > (alem do destaque)
      if (i == selected) {
        putChar(x + 1, yy, '<', A_SELECTED);
        putChar(x + inner, yy, '>', A_SELECTED);
      }
    }
    // indicadores de rolagem
    if (top > 0)
      putChar(x + w - 1, y + 1, C_UP, A_FRAME);
    if (top + visible < count)
      putChar(x + w - 1, y + h - 2, C_DOWN, A_FRAME);

    if (s_menuInfo)
      s_menuInfo(selected);

    VirtualKeyItem k;
    s_monitorChanged = false;
    getKey(&k);
    auto vk = k.vk;
    if (s_monitorChanged) {
      s_lastSelected = selected;
      result = -2;         // monitor mudou: quem chamou redesenha e chama de novo
      break;               // (sai pelo fim: libera a lista e a tela salva)
    }

    if (isUp(vk)) {
      int s = selected;
      do { s = (s - 1 + count) % count; } while (list[s].separator && s != selected);
      selected = s;
    } else if (isDown(vk)) {
      int s = selected;
      do { s = (s + 1) % count; } while (list[s].separator && s != selected);
      selected = s;
    } else if (isHome(vk)) {
      selected = 0;
      while (list[selected].separator && selected < count - 1) ++selected;
    } else if (isEnd(vk)) {
      selected = count - 1;
      while (list[selected].separator && selected > 0) --selected;
    } else if (isPgUp(vk)) {
      selected = selected - visible < 0 ? 0 : selected - visible;
      while (list[selected].separator && selected < count - 1) ++selected;
    } else if (isPgDn(vk)) {
      selected = selected + visible >= count ? count - 1 : selected + visible;
      while (list[selected].separator && selected > 0) --selected;
    } else if (isEnter(vk)) {
      result = selected;
      if (exitKey)
        *exitKey = (int) VirtualKey::VK_RETURN;
      break;
    } else if (vk == VirtualKey::VK_ESCAPE) {
      result = -1;
      if (exitKey)
        *exitKey = (int) VirtualKey::VK_ESCAPE;
      break;
    } else if (exitKey && (vk == VirtualKey::VK_F2 || vk == VirtualKey::VK_DELETE || vk == VirtualKey::VK_KP_DELETE || vk == VirtualKey::VK_F7 || vk == VirtualKey::VK_F4 || vk == VirtualKey::VK_F5 || vk == VirtualKey::VK_F8)) {
      result = selected;
      *exitKey = (vk == VirtualKey::VK_KP_DELETE ? (int) VirtualKey::VK_DELETE : (int) vk);
      break;
    } else if (k.ASCII > ' ') {
      // pula para o proximo item que comeca com a letra
      char c = toupper(k.ASCII);
      for (int d = 1; d <= count; ++d) {
        int s = (selected + d) % count;
        if (!list[s].separator && toupper(list[s].label[0] == '[' && list[s].labelLen > 1 ? list[s].label[1] : list[s].label[0]) == c) {
          selected = s;
          break;
        }
      }
    }
  }

  restoreScreen(saved);
  free(list);
  return result;
}


int menu(char const * title, char const * items, int selected, int width)
{
  return menuEx(title, items, selected, width, nullptr, nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// mensagens


// conta linhas e a maior linha de um texto com '\n'
static void measure(char const * text, int * lines, int * maxw)
{
  *lines = 1;
  *maxw  = 0;
  int cur = 0;
  for (auto p = text; *p; ++p) {
    if (*p == '\n') {
      ++*lines;
      cur = 0;
    } else if (++cur > *maxw)
      *maxw = cur;
  }
}


static void printLines(int x, int y, int w, char const * text, uint8_t attr)
{
  int line = 0;
  char const * p = text;
  while (true) {
    char const * end = strchr(p, '\n');
    int len = end ? end - p : strlen(p);
    int tx = x + (w - len) / 2;
    print(tx, y + line, p, attr, len);
    if (!end)
      break;
    p = end + 1;
    ++line;
  }
}


static void drawButtons(int x, int y, int w, char const * const * labels, int count, int focused, uint8_t windowAttr)
{
  int total = 0;
  for (int i = 0; i < count; ++i)
    total += strlen(labels[i]) + 4 + (i ? 2 : 0);
  int bx = x + (w - total) / 2;
  for (int i = 0; i < count; ++i) {
    // botao com foco: < Yes > com destaque; sem foco:   Yes   na cor da janela
    char tmp[32];
    if (i == focused)
      snprintf(tmp, sizeof(tmp), "< %s >", labels[i]);
    else
      snprintf(tmp, sizeof(tmp), "  %s  ", labels[i]);
    print(bx, y, tmp, i == focused ? A_BUTTONSEL : windowAttr);
    bx += strlen(tmp) + 2;
  }
}


static void dialogBox(char const * title, char const * text, bool error, int extraLines, int * outX, int * outY, int * outW, int * outH)
{
  int lines, maxw;
  measure(text, &lines, &maxw);
  int w = maxw + 6;
  if (title && (int) strlen(title) + 6 > w)
    w = strlen(title) + 6;
  if (w < 30)
    w = 30;
  if (w > COLS - 4)
    w = COLS - 4;
  int h = lines + 4 + extraLines;
  int x = (COLS - w) / 2;
  int y = (ROWS - h) / 2;
  box(x, y, w, h, error ? A_ERRFRAME : A_FRAME, error ? A_ERROR : A_WINDOW, title, error ? A_ERROR : A_TITLE);
  printLines(x, y + 1, w, text, error ? A_ERROR : A_WINDOW);
  *outX = x; *outY = y; *outW = w; *outH = h;
}


void message(char const * title, char const * text, bool error)
{
  auto saved = saveScreen();
  setHelp("~Enter~ OK");
  int x, y, w, h;
  dialogBox(title, text, error, 0, &x, &y, &w, &h);
  char const * labels[] = { "OK" };
  drawButtons(x, y + h - 2, w, labels, 1, 0, error ? A_ERROR : A_WINDOW);
  VirtualKeyItem k;
  do {
    getKey(&k);
  } while (!isEnter(k.vk) && k.vk != VirtualKey::VK_ESCAPE && k.vk != VirtualKey::VK_SPACE);
  restoreScreen(saved);
}


bool confirm(char const * title, char const * text, bool defaultYes)
{
  auto saved = saveScreen();
  setHelp("~\x1b\x1a~ Choose   ~Enter~ OK   ~Esc~ No");
  int x, y, w, h;
  dialogBox(title, text, false, 0, &x, &y, &w, &h);
  char const * labels[] = { "No", "Yes" };
  int focused = defaultYes ? 1 : 0;
  bool result = false;
  while (true) {
    drawButtons(x, y + h - 2, w, labels, 2, focused, A_WINDOW);
    VirtualKeyItem k;
    getKey(&k);
    if (isLeft(k.vk) || isRight(k.vk) || k.vk == VirtualKey::VK_TAB)
      focused ^= 1;
    else if (isEnter(k.vk) || k.vk == VirtualKey::VK_SPACE) {
      result = (focused == 1);
      break;
    } else if (k.vk == VirtualKey::VK_ESCAPE) {
      result = false;
      break;
    } else if (toupper(k.ASCII) == 'Y' || toupper(k.ASCII) == 'S') {
      result = true;
      break;
    } else if (toupper(k.ASCII) == 'N') {
      result = false;
      break;
    }
  }
  restoreScreen(saved);
  return result;
}


bool input(char const * title, char const * label, char * buf, int maxlen)
{
  auto saved = saveScreen();
  setHelp("~Enter~ OK  ~Esc~ Cancel  ~\x1b\x1a~ Move  ~Home~/~End~  ~Del~/~Backspace~ Erase");

  constexpr int FIELDW = 40;
  int w = FIELDW + 6;
  int h = 6;
  int x = (COLS - w) / 2;
  int y = (ROWS - h) / 2;
  box(x, y, w, h, A_FRAME, A_WINDOW, title);
  print(x + 3, y + 1, label, A_WINDOW, w - 6);

  // trabalha numa copia (Esc descarta)
  char * tmp = (char *) malloc(maxlen + 1);
  if (!tmp) {
    restoreScreen(saved);
    return false;
  }
  strncpy(tmp, buf, maxlen);
  tmp[maxlen] = 0;
  int len = strlen(tmp);
  int cur = len;          // posicao do cursor (sempre insercao)
  int start = 0;          // primeiro caractere visivel
  bool result = false;
  const uint8_t A_CURSOR = (uint8_t)(((A_INPUT & 0x0f) << 4) | (A_INPUT >> 4));

  while (true) {
    // rolagem horizontal: cursor sempre visivel
    if (cur < start)
      start = cur;
    if (cur > start + FIELDW - 1)
      start = cur - (FIELDW - 1);
    fill(x + 3, y + 3, FIELDW, 1, ' ', A_INPUT);
    print(x + 3, y + 3, tmp + start, A_INPUT, FIELDW);
    // cursor: caractere sob ele em cores invertidas (no fim: bloco)
    if (cur < len)
      putChar(x + 3 + (cur - start), y + 3, (uint8_t) tmp[cur], A_CURSOR);
    else
      putChar(x + 3 + (cur - start), y + 3, C_FULL, A_INPUT);

    VirtualKeyItem k;
    getKey(&k);
    auto vk = k.vk;
    if (isEnter(vk)) {
      strcpy(buf, tmp);
      result = true;
      break;
    } else if (vk == VirtualKey::VK_ESCAPE) {
      break;
    } else if (vk == VirtualKey::VK_LEFT || vk == VirtualKey::VK_KP_LEFT) {
      if (cur > 0) --cur;
    } else if (vk == VirtualKey::VK_RIGHT || vk == VirtualKey::VK_KP_RIGHT) {
      if (cur < len) ++cur;
    } else if (vk == VirtualKey::VK_HOME || vk == VirtualKey::VK_KP_HOME) {
      cur = 0;
    } else if (vk == VirtualKey::VK_END || vk == VirtualKey::VK_KP_END) {
      cur = len;
    } else if (vk == VirtualKey::VK_BACKSPACE) {
      if (cur > 0) {
        memmove(tmp + cur - 1, tmp + cur, len - cur + 1);
        --cur;
        --len;
      }
    } else if (vk == VirtualKey::VK_DELETE || vk == VirtualKey::VK_KP_DELETE) {
      if (cur < len) {
        memmove(tmp + cur, tmp + cur + 1, len - cur);
        --len;
      }
    } else if (k.ASCII >= ' ' && k.ASCII < 0x7f && len < maxlen) {
      memmove(tmp + cur + 1, tmp + cur, len - cur + 1);
      tmp[cur++] = k.ASCII;
      ++len;
    }
  }
  free(tmp);
  restoreScreen(saved);
  return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// progresso / informacao


static int s_progX, s_progY, s_progW;


void progressBegin(char const * title, char const * text)
{
  setHelp("Please wait...");
  int x, y, w, h;
  dialogBox(title, text, false, 2, &x, &y, &w, &h);
  s_progX = x + 3;
  s_progY = y + h - 3;
  s_progW = w - 6;
  progressUpdate(0);
}


void progressUpdate(int percent)
{
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  int barw = s_progW - 5;
  int full = barw * percent / 100;
  for (int i = 0; i < barw; ++i)
    putChar(s_progX + i, s_progY, i < full ? C_FULL : C_LIGHT, A_WINDOW);
  char tmp[8];
  snprintf(tmp, sizeof(tmp), "%3d%%", percent);
  print(s_progX + barw + 1, s_progY, tmp, A_WINDOW);
}


void info(char const * title, char const * text)
{
  int x, y, w, h;
  dialogBox(title, text, false, -1, &x, &y, &w, &h);
}


}  // namespace xui
