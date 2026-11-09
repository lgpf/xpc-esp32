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


/*
   XPC Cobra - PC/XT para ESP32 (TTGO VGA32 v1.4), baseado no PCEmulator da FabGL

    - placa ESP32 com PSRAM e cartao SD (ex.: TTGO VGA32 v1.4)
    - Arduino: core ESP32 2.0.x, PSRAM *DESABILITADO* (e ativado em tempo de execucao)
    - Partition Scheme: Huge APP

   Interface toda em modo texto 80x25 (xui.h): tela de inicio, mensagens e menu de setup.
   Menu de setup: ALT + PRINTSCREEN ou CTRL + ALT + UP
   Ciclo do monitor (colorido / verde / ambar / branco): CTRL + ALT + DOWN
*/


#pragma message "This sketch requires Tools->Partition Scheme = Huge APP"


#include <memory>
#include <vector>
#include <algorithm>
#include <string>

#include "esp32-hal-psram.h"
extern "C" {
#include "esp_spiram.h"
}

#include <Preferences.h>

#include "fabgl.h"

#include "mconf.h"
#include "machine.h"
#include "driver/dac.h"
#include "driver/periph_ctrl.h"
#include "soc/sens_reg.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"
#include "xui.h"



// UART Pins for USB serial
#define UART_URX 3
#define UART_UTX 1



using std::unique_ptr;

using fabgl::StringList;
using fabgl::imin;
using fabgl::imax;



Preferences   preferences;
Machine     * machine;


// noinit! Used to maintain datetime between reboots
__NOINIT_ATTR static timeval savedTimeValue;


// descricao da configuracao em uso (barra de titulo do menu)
static char s_confDesc[64] = "";



////////////////////////////////////////////////////////////////////////////////////////////////////
// data e hora


// handle soft restart
void shutdownHandler()
{
  // save current datetime into Preferences
  gettimeofday(&savedTimeValue, nullptr);
}


void updateDateTime()
{
  // Set timezone
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  // get datetime from savedTimeValue? (noinit section)
  if (esp_reset_reason() == ESP_RST_SW) {
    // adjust time taking account elapsed time since ESP32 started
    savedTimeValue.tv_usec += (int) esp_timer_get_time();
    savedTimeValue.tv_sec  += savedTimeValue.tv_usec / 1000000;
    savedTimeValue.tv_usec %= 1000000;
    settimeofday(&savedTimeValue, nullptr);
    return;
  }

  // sem WiFi: data padrao
  auto tm = (struct tm){ .tm_sec  = 0, .tm_min  = 0, .tm_hour = 8, .tm_mday = 14, .tm_mon  = 7, .tm_year = 84 };
  auto now = (timeval){ .tv_sec = mktime(&tm) };
  settimeofday(&now, nullptr);
}



////////////////////////////////////////////////////////////////////////////////////////////////////
// discos


// retorna o nome do arquivo se existe no SD (URLs nao sao suportadas: sem WiFi)
char const * getDisk(char const * url)
{
  if (url && *url && strncmp("http://", url, 7) && strncmp("https://", url, 8)) {
    FileBrowser fb(SD_MOUNT_PATH);
    if (fb.filePathExists(url))
      return url;
  }
  return nullptr;
}


// XPC: pastas do SD: imagens de HD em \HD, disquetes em \DISK (o gerenciador de disquetes nao sai de \DISK)
#define HD_DIR    "HD"
#define DISK_DIR  "DISK"
static char const * DISK_ROOT = SD_MOUNT_PATH "/" DISK_DIR;


// extensoes aceitas como imagem de disco
static bool isDiskImage(char const * name)
{
  auto ext = strrchr(name, '.');
  if (!ext)
    return false;
  return strcasecmp(ext, ".img") == 0 || strcasecmp(ext, ".ima") == 0 || strcasecmp(ext, ".dsk") == 0 ||
         strcasecmp(ext, ".vfd") == 0 || strcasecmp(ext, ".flp") == 0;
}


// nome de imagem: o usuario edita so o nome (sem extensao) e a extensao e anexada aqui
//   tira espacos e pontos das pontas, recusa caracteres proibidos no FAT; se o usuario digitou
//   a mesma extensao por habito ("jogo.img"), ela nao e repetida
// retorna false (com mensagem) se o nome nao pode ser usado
static bool fixImageName(char * name, int maxlen, char const * ext, char const * title)
{
  int b = 0, e = strlen(name);
  while (name[b] == ' ' || name[b] == '.') ++b;
  while (e > b && (name[e - 1] == ' ' || name[e - 1] == '.')) --e;
  memmove(name, name + b, e - b);
  name[e - b] = 0;
  int n = strlen(name), el = strlen(ext);
  if (n > el && strcasecmp(name + n - el, ext) == 0)
    name[n - el] = 0;
  if (!*name) {
    xui::message(title, "Empty file name!", true);
    return false;
  }
  if (strpbrk(name, "\\/:*?\"<>|")) {
    xui::message(title, "Invalid character in name:  \\ / : * ? \" < > |", true);
    return false;
  }
  if ((int)(strlen(name) + el) >= maxlen) {
    xui::message(title, "Name too long!", true);
    return false;
  }
  strcat(name, ext);
  return true;
}

// nome sem extensao (para o campo de edicao)
static void baseName(char const * name, char * out, int maxlen)
{
  auto ext = strrchr(name, '.');
  snprintf(out, maxlen, "%.*s", (int)(ext ? ext - name : strlen(name)), name);
}


// caminho relativo ao SD (sem "/SD/") de um item da pasta atual
static void relativePath(FileBrowser & fb, char const * name, char * out, int maxlen)
{
  char const * dir = fb.directory() + strlen(SD_MOUNT_PATH);   // "" ou "/pasta/..."
  while (*dir == '/')
    ++dir;
  if (*dir)
    snprintf(out, maxlen, "%s/%s", dir, name);
  else
    snprintf(out, maxlen, "%s", name);
}


// desmonta o arquivo de qualquer drive de disquete onde estiver montado
static void unmountIfMounted(char const * path)
{
  for (int d = 0; d < 2; ++d)
    if (machine->diskFilename(d) && strcasecmp(machine->diskFilename(d), path) == 0)
      machine->setDriveImage(d, nullptr);
}


// nome curto do disco montado (sem pasta), ou "(empty)"
static void diskLabel(int drive, char * out, int maxlen)
{
  auto f = machine->diskFilename(drive);
  if (!f || !machine->disk(drive)) {
    snprintf(out, maxlen, "(empty)");
    return;
  }
  auto p = strrchr(f, '/');
  // descricao do mconfs.txt; sem descricao, o nome do arquivo
  char desc[60];
  mconfGetDesc(p ? p + 1 : f, desc, sizeof(desc));
  snprintf(out, maxlen, "%s", *desc ? desc : (p ? p + 1 : f));
  for (char * c = out; *c; ++c)
    if (*c == ';' || *c == '\t')
      *c = ',';
  // cabe ao lado do rotulo (menu de 42 colunas): corta e marca com >>
  constexpr int MAXW = 25;
  if ((int) strlen(out) > MAXW) {
    out[MAXW - 1] = '\xAF';
    out[MAXW] = 0;
  }
}



////////////////////////////////////////////////////////////////////////////////////////////////////
// menu de setup (modo texto)


// configuracoes salvas na flash (Preferences)
//   video : 0 = CGA, 1 = Hercules, 2 = EGA
//   sound : 0 = speaker, 1 = speaker + AdLib
//   kbd   : 0 = US, 1 = ABNT2
// padrao: CGA / Speaker / US (o monitor sempre inicia em verde)
static const char * VIDEONAMES[]   = { "CGA", "Hercules", "EGA", "EGA 640x350" };
static const char * SOUNDNAMES[]   = { "Speaker", "Speaker + AdLib" };
static const char * KBDNAMES[]     = { "US", "ABNT2" };
static const int    RAMSIZES[]     = { 256, 512, 640, 704 };
static const char * MONITORNAMES[] = { "Color", "Green", "Amber", "White" };


// XPC: perfis de maquina (so a apresentacao muda: tela de boot e titulo do menu)
//   0 = Cobra XPC, 1 = IBM PC XT
static int s_machine = 0;
static const char * MACHINENAMES[] = { "Cobra XPC", "IBM PC XT" };
static const char * MACHINETITLES[] = { "Cobra XPC - V 3.02", "IBM Personal Computer XT" };


// mensagem da BIOS "sem disco de boot": portugues no Cobra XPC, ingles no IBM PC XT
static void setBootMessageLanguage()
{
  machine->setBootMsgPT(s_machine == 0);
}


static int  s_mousePortMode   = 0;       // Setup > Mouse port: 0 = Auto, 1 = mouse PS/2, 2 = serial COM1 (modem)
static bool s_mousePortSerial = false;   // em uso agora: true = serial
static const char * MPORTNAMES[] = { "Auto (mouse)", "Auto (serial)", "PS/2 mouse", "Serial COM1" };



// Setup: quadro a direita com a descricao do item selecionado (mesmo estilo do quadro de Debug)
static char const * const SETUPINFO[] = {
  "Back to the emulated PC.",                                                   // Continue
  "",
  "Disk image in drive A: (insert, eject, protect, copy).",                     // Floppy A
  "Disk image in drive B: (insert, eject, protect, copy).",                     // Floppy B
  "Hard disk image C: (choose, create, rename, copy, delete, protect).",       // HD
  "",
  "Boot screen and messages: Cobra XPC or IBM PC XT.",                          // Machine
  "Conventional memory. 704K only with CGA or Hercules.",                       // Memory
  "CGA, Hercules or EGA. Restarts the emulator.",                               // Video
  "PC speaker, or speaker plus AdLib (OPL2) music.",                            // Sound
  "Keyboard layout: US or ABNT2.",                                              // Keyboard
  "PS/2 port: mouse, serial WiFi modem on COM1, or auto.",                      // Mouse port
  "Color, green, amber, gray (CTRL+ALT+DOWN). CGA palette: CTRL+ALT+RIGHT next, LEFT original.",             // Monitor
  "On: late timer ticks are delivered (music, e.g. Digger). Off: lost like a real XT (e.g. Arkanoid II).", // Timer sync
  "Auto: notes as Classic, digitized sound as PCM. PCM: every pulse. Classic: measured note.", // PC speaker
  "Shows a box with memory, speaker and mouse readings.",                       // Debug
  "",
  "Reset the emulated PC, like CTRL+ALT+DEL.",                                  // Restart machine
  "Restart the whole ESP32 (applies all settings).",                            // Restart emulator
};

static void setupInfo(int sel)
{
  constexpr int X = 63, Y = 2, W = 17, H = 12, TW = W - 4;
  xui::box(X, Y, W, H, xui::A_FRAME, xui::A_WINDOW, " Info ", xui::A_FRAME, true);
  char const * p = (sel >= 0 && sel < (int)(sizeof(SETUPINFO) / sizeof(SETUPINFO[0]))) ? SETUPINFO[sel] : "";
  for (int row = 0; *p && row < H - 2; ++row) {
    int len = strlen(p), cut = len;
    if (len > TW) {
      cut = TW;
      while (cut > 0 && p[cut] != ' ')
        --cut;
      if (cut == 0)
        cut = TW;
    }
    xui::print(X + 2, Y + 1 + row, p, xui::A_WINDOW, cut);
    p += cut;
    while (*p == ' ')
      ++p;
  }
}

static char const * const SPKMODES[] = { "Auto", "PCM", "Classic" };
static bool s_debug = false;   // Setup > Debug: quadro com medidas no canto do menu (padrao: desligado)

static void drawDesktop()
{
  xui::desktop(s_confDesc);
  // creditos (sempre), abaixo do quadro de Debug
  xui::box(0, 13, 17, 6, 0x2F, 0x2F, nullptr, 0x2F, true);   // branco sobre verde
  static char const * const CREDITS[] = { "RetroLabs", "JL Martins", "Luiz Pacheco", "2026" };
  for (int i = 0; i < 4; ++i)
    xui::print(1 + (15 - (int) strlen(CREDITS[i])) / 2, 14 + i, CREDITS[i], 0x2F);
  if (s_debug) {
    char t[24];
    auto k = machine->getI8042();
    xui::box(0, 2, 17, 8, xui::A_ERRFRAME, xui::A_ERROR, " Debug ", xui::A_ERRFRAME, true);
    snprintf(t, sizeof(t), "RAM %uK/%uK", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
    xui::print(2, 3, t, xui::A_ERROR, 14);
    snprintf(t, sizeof(t), "SPK %lu/%d", (unsigned long)machine->m_bbPulses, machine->m_bbRawFreq);
    xui::print(2, 4, t, xui::A_ERROR, 14);
    snprintf(t, sizeof(t), "MOU %c c%02X", k->diagMouseOk() ? 'Y' : 'N', k->diagCmdByte());
    xui::print(2, 5, t, xui::A_ERROR, 14);
    snprintf(t, sizeof(t), " p%lu", (unsigned long)k->m_diagPackets);
    xui::print(2, 6, t, xui::A_ERROR, 14);
    extern uint32_t g_diagC2;
    snprintf(t, sizeof(t), " i%lu C2 %02X", (unsigned long)k->m_diagIrqs, (unsigned)(g_diagC2 >> 24));
    xui::print(2, 7, t, xui::A_ERROR, 14);
    snprintf(t, sizeof(t), "%02X %02X id%02X", (unsigned)((g_diagC2 >> 16) & 0xff), (unsigned)((g_diagC2 >> 8) & 0xff), (unsigned)(g_diagC2 & 0xff));
    xui::print(2, 8, t, xui::A_ERROR, 14);
  }
}


// itens que o gerenciador nao mostra: ".." em \DISK (nao sobe para a raiz) e as imagens dos HDs em uso
static bool hiddenItem(FileBrowser & fb, fabgl::DirItem const * it)
{
  bool atRoot = strcasecmp(fb.directory(), DISK_ROOT) == 0;
  if (it->isDir)
    return atRoot && strcmp(it->name, "..") == 0;
  char path[128];
  relativePath(fb, it->name, path, sizeof(path));
  for (int d = 2; d < DISKCOUNT; ++d)
    if (machine->diskFilename(d) && strcasecmp(machine->diskFilename(d), path) == 0)
      return true;
  return false;
}


static void restartEmulator();


// ---- protecao: lista "XPC.PRT" em cada pasta (um nome por linha). Protegido nao apaga nem renomeia ----

static void protFile(char const * dir, char * out, int maxlen)
{
  snprintf(out, maxlen, "%s/XPC.PRT", dir);
}


static bool isProtected(char const * dir, char const * name)
{
  char pf[160], line[80];
  protFile(dir, pf, sizeof(pf));
  auto f = fopen(pf, "r");
  if (!f)
    return false;
  bool found = false;
  while (!found && fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    found = strcasecmp(line, name) == 0;
  }
  fclose(f);
  return found;
}


static void setProtected(char const * dir, char const * name, bool value)
{
  char pf[160], tmp[160], line[80];
  protFile(dir, pf, sizeof(pf));
  snprintf(tmp, sizeof(tmp), "%s/XPC.TMP", dir);
  auto out = fopen(tmp, "w");
  if (!out)
    return;
  if (auto in = fopen(pf, "r")) {
    while (fgets(line, sizeof(line), in)) {
      line[strcspn(line, "\r\n")] = 0;
      if (*line && strcasecmp(line, name))
        fprintf(out, "%s\n", line);
    }
    fclose(in);
  }
  if (value)
    fprintf(out, "%s\n", name);
  fclose(out);
  remove(pf);
  rename(tmp, pf);
}


// arquivo de geometria de um HD ("nome.chs"), ao lado da imagem
static void chsName(char const * name, char * out, int maxlen)
{
  snprintf(out, maxlen, "%s", name);
  auto dot = strrchr(out, '.');
  if (dot)
    *dot = 0;
  if (strlen(out) + 4 < (size_t) maxlen)
    strcat(out, ".chs");
}


// copia um arquivo mostrando o progresso
static bool copyFile(char const * src, char const * dst)
{
  auto in = fopen(src, "rb");
  if (!in)
    return false;
  auto out = fopen(dst, "wb");
  if (!out) {
    fclose(in);
    return false;
  }
  fseek(in, 0, SEEK_END);
  long size = ftell(in);
  fseek(in, 0, SEEK_SET);
  constexpr int BUFSIZE = 4096;
  auto buf = (uint8_t*) malloc(BUFSIZE);
  bool ok = buf != nullptr;
  long done = 0;
  int lastPct = -1;
  while (ok && done < size) {
    int n = fread(buf, 1, BUFSIZE, in);
    if (n <= 0 || (int) fwrite(buf, 1, n, out) != n)
      ok = false;
    done += n;
    int pct = size ? (int)(done * 100 / size) : 100;
    if (pct != lastPct) {
      xui::progressUpdate(pct);
      lastPct = pct;
    }
  }
  free(buf);
  fclose(in);
  fclose(out);
  if (!ok)
    remove(dst);
  return ok;
}


// cria uma imagem de HD vazia (zeros) e o arquivo .chs com a geometria
static bool createHDImage(char const * dir, char const * name, int cyl, int heads, int spt)
{
  char full[160];
  snprintf(full, sizeof(full), "%s/%s", dir, name);
  auto f = fopen(full, "wb");
  if (!f)
    return false;
  constexpr int BUFSIZE = 4096;
  auto buf = (uint8_t*) calloc(1, BUFSIZE);
  long size = (long) cyl * heads * spt * 512;
  bool ok = buf != nullptr;
  int lastPct = -1;
  for (long done = 0; ok && done < size; done += BUFSIZE) {
    if (fwrite(buf, 1, BUFSIZE, f) != BUFSIZE)
      ok = false;
    int pct = (int)((done + BUFSIZE) * 100 / size);
    if (pct != lastPct) {
      xui::progressUpdate(pct);
      lastPct = pct;
    }
  }
  free(buf);
  fclose(f);
  if (!ok) {
    remove(full);
    return false;
  }
  char chs[80], chsFull[160];
  chsName(name, chs, sizeof(chs));
  snprintf(chsFull, sizeof(chsFull), "%s/%s", dir, chs);
  if (auto c = fopen(chsFull, "w")) {
    fprintf(c, "%d,%d,%d\n", cyl, heads, spt);
    fclose(c);
  }
  return true;
}


// o arquivo esta em uso (disquete montado ou HD da sessao atual)?
static bool inUse(char const * path, bool hdOnly)
{
  for (int d = hdOnly ? 2 : 0; d < DISKCOUNT; ++d)
    if (machine->diskFilename(d) && strcasecmp(machine->diskFilename(d), path) == 0)
      return true;
  return false;
}


// ---- mconfs.txt: descricao dos HDs de \HD (a linha com hd0 "HD/arquivo") ----

// procura 'key "' na linha e devolve inicio/fim do texto entre aspas
static bool quoted(char const * line, char const * key, int * b, int * e)
{
  auto k = strstr(line, key);
  if (!k)
    return false;
  auto q1 = strchr(k + strlen(key), '"');
  if (!q1)
    return false;
  auto q2 = strchr(q1 + 1, '"');
  if (!q2)
    return false;
  *b = q1 + 1 - line;
  *e = q2 - line;
  return true;
}


// a linha usa o HD 'name' (compara so o nome do arquivo)?
static bool lineIsHD(char const * line, char const * name)
{
  int b, e;
  if (!quoted(line, "hd0", &b, &e))
    return false;
  char path[128];
  snprintf(path, sizeof(path), "%.*s", e - b, line + b);
  auto base = strrchr(path, '/');
  return strcasecmp(base ? base + 1 : path, name) == 0;
}


// descricao do HD 'name' no mconfs.txt (vazio se nao houver linha)
static void mconfGetDesc(char const * name, char * out, int maxlen)
{
  *out = 0;
  auto f = fopen(SD_MOUNT_PATH "/" MACHINE_CONF_FILENAME, "r");
  if (!f)
    return;
  char line[256];
  int b, e;
  while (fgets(line, sizeof(line), f))
    if (lineIsHD(line, name) && quoted(line, "desc", &b, &e)) {
      snprintf(out, maxlen, "%.*s", e - b, line + b);
      break;
    }
  fclose(f);
}


// atualiza o mconfs.txt para o HD 'name':
//   newName: renomeado (troca o arquivo na linha); desc: nova descricao (cria a linha se nao existir);
//   remove: apaga a linha. chs: geometria para uma linha nova (0 = sem chs0)
static void mconfSetHD(char const * name, char const * newName, char const * desc, bool remove, int c = 0, int h = 0, int sp = 0)
{
  char tmp[64], conf[64];
  snprintf(conf, sizeof(conf), SD_MOUNT_PATH "/" MACHINE_CONF_FILENAME);
  snprintf(tmp, sizeof(tmp), SD_MOUNT_PATH "/MCONFS.TMP");
  auto out = fopen(tmp, "w");
  if (!out)
    return;
  bool found = false;
  char line[256];
  if (auto in = fopen(conf, "r")) {
    while (fgets(line, sizeof(line), in)) {
      if (!lineIsHD(line, name)) {
        fputs(line, out);
        continue;
      }
      found = true;
      if (remove)
        continue;
      char buf[320];
      snprintf(buf, sizeof(buf), "%s", line);
      int b, e;
      if (newName && quoted(buf, "hd0", &b, &e)) {
        char nb[320];
        snprintf(nb, sizeof(nb), "%.*s" HD_DIR "/%s%s", b, buf, newName, buf + e);
        strcpy(buf, nb);
      }
      if (desc && quoted(buf, "desc", &b, &e)) {
        char nb[320];
        snprintf(nb, sizeof(nb), "%.*s%s%s", b, buf, desc, buf + e);
        strcpy(buf, nb);
      }
      fputs(buf, out);
    }
    fclose(in);
  }
  if (!found && !remove && desc) {
    fprintf(out, "\ndesc \"%s\" hd0 \"" HD_DIR "/%s\"", desc, newName ? newName : name);
    if (c && h && sp)
      fprintf(out, " chs0 %d,%d,%d", c, h, sp);
    fprintf(out, " boot hd0\n");
  }
  fclose(out);
  ::remove(conf);
  rename(tmp, conf);
}


// pede a descricao do HD (titulo que aparece na barra do Setup)
static bool askHDDesc(char const * name, char * desc, int maxlen)
{
  if (!*desc) {
    snprintf(desc, maxlen, "%s", name);
    auto dot = strrchr(desc, '.');
    if (dot)
      *dot = 0;
  }
  return xui::input("Hard disk description (mconfs.txt)", name, desc, maxlen - 1) && *desc;
}


// gerenciador de arquivos
//   drive 0/1 = disquete A/B (pasta \DISK e subpastas), drive 2 = HD (pasta \HD)
//   Enter = montar (disquete) / escolher para o proximo boot (HD), F2 = renomear, F4 = proteger,
//   F5 = duplicar, F7 = nova pasta (disquete) ou novo HD, F8 = ejetar (disquete), Del = apagar, Esc = voltar
// retorna true se montou/escolheu uma imagem
// gerenciador de HD: a lista mostra a descricao (mconfs.txt); o quadro embaixo mostra o nome do arquivo
static std::vector<std::string> s_fmNames;   // arquivo de cada linha da lista ("" = pasta)
static std::string s_fmDir;                  // pasta da lista (caminho completo)
static bool s_fmHD = false;

// geometria tipica de um disquete pelo tamanho da imagem
static char const * floppyKind(long size)
{
  switch (size / 1024) {
    case 160:  return "5.25\" SS 40/1/8";
    case 180:  return "5.25\" SS 40/1/9";
    case 320:  return "5.25\" DD 40/2/8";
    case 360:  return "5.25\" DD 40/2/9";
    case 720:  return "3.5\" DD 80/2/9";
    case 1200: return "5.25\" HD 80/2/15";
    case 1440: return "3.5\" HD 80/2/18";
    case 2880: return "3.5\" ED 80/2/36";
    default:   return "non standard size";
  }
}

// quadro abaixo da lista: nome do arquivo, tamanho e geometria
static void fmFileInfo(int sel)
{
  constexpr int W = 46, X = (80 - W) / 2, Y = 19;
  xui::box(X, Y, W, 4, xui::A_FRAME, xui::A_WINDOW, " File ", xui::A_FRAME, true);
  if (sel < 0 || sel >= (int) s_fmNames.size())
    return;
  auto & nm = s_fmNames[sel];
  if (nm.empty()) {
    xui::print(X + 2, Y + 1, "Folder", xui::A_WINDOW, W - 4);
    return;
  }
  xui::print(X + 2, Y + 1, nm.c_str(), xui::A_WINDOW, W - 4);
  std::string full = s_fmDir + "/" + nm;
  long size = 0;
  if (auto f = fopen(full.c_str(), "rb")) {
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fclose(f);
  }
  char line[64];
  if (s_fmHD) {
    uint16_t c, h, sc;
    hdGeometry((std::string(HD_DIR "/") + nm).c_str(), nullptr, &c, &h, &sc);
    if (c)
      snprintf(line, sizeof(line), "%ld.%ld MB   CHS %u/%u/%u (.chs)", size / 1048576, (size % 1048576) * 10 / 1048576, c, h, sc);
    else
      snprintf(line, sizeof(line), "%ld.%ld MB   CHS automatic", size / 1048576, (size % 1048576) * 10 / 1048576);
  } else
    snprintf(line, sizeof(line), "%ld KB   %s", size / 1024, floppyKind(size));
  if (isProtected(s_fmDir.c_str(), nm.c_str()))
    strncat(line, "   *protected", sizeof(line) - strlen(line) - 1);
  xui::print(X + 2, Y + 2, line, xui::A_WINDOW, W - 4);
}


static bool fileManager(int drive)
{
  constexpr int MAXPATH = 128;
  bool hdMode = (drive == 2);
  FileBrowser fb;
  fb.setDirectory(hdMode ? SD_MOUNT_PATH "/" HD_DIR : DISK_ROOT);
  int sel = 0;
  bool first = true;   // na primeira lista, posiciona no disco em uso

  while (true) {

    // lista: pastas entre colchetes, depois as imagens (protegidas com '*'), em ordem alfabetica
    //   HD: pela descricao do mconfs.txt (sem descricao: pelo nome do arquivo); disquete: pelo nome
    //   'map' liga a linha ao item do FileBrowser
    struct Entry { std::string key, text, name; int idx; bool dir; };
    std::vector<Entry> ents;
    int count = fb.count();
    for (int i = 0; i < count; ++i) {
      auto it = fb.get(i);
      if (hdMode ? it->isDir : hiddenItem(fb, it))
        continue;
      if (it->isDir) {
        ents.push_back({ it->name, std::string("[") + it->name + "]", "", i, true });
      } else if (isDiskImage(it->name)) {
        char desc[60] = "";
        if (hdMode) {
          mconfGetDesc(it->name, desc, sizeof(desc));
          for (char * c = desc; *c; ++c)
            if (*c == ';' || *c == '\t')
              *c = ',';
        }
        std::string shown = *desc ? desc : it->name;
        ents.push_back({ shown, std::string(isProtected(fb.directory(), it->name) ? "*" : " ") + shown, it->name, i, false });
      }
    }
    std::sort(ents.begin(), ents.end(), [](Entry const & a, Entry const & b) {
      if (a.dir != b.dir)
        return a.dir;                       // pastas primeiro (".." no topo)
      return strcasecmp(a.key.c_str(), b.key.c_str()) < 0;
    });
    unique_ptr<int[]> map(new int[ents.size() + 1]);
    std::string list;
    s_fmNames.clear();
    s_fmDir = fb.directory();
    s_fmHD  = hdMode;
    int n = 0;
    for (auto & e : ents) {
      if (n)
        list += ";";
      list += e.text;
      s_fmNames.push_back(e.name);
      if (first && !e.dir) {
        char p[128];
        relativePath(fb, e.name.c_str(), p, sizeof(p));
        if (machine->diskFilename(drive) && strcasecmp(machine->diskFilename(drive), p) == 0)
          sel = n;
      }
      map[n++] = e.idx;
    }
    unique_ptr<char[]> items(new char[list.size() + 64]);
    strcpy(items.get(), list.c_str());

    first = false;

    char title[MAXPATH];
    if (hdMode)
      snprintf(title, sizeof(title), "Hard disk  SD:/" HD_DIR);
    else
      snprintf(title, sizeof(title), "Floppy %c:  SD:%s", 'A' + drive, fb.directory() + strlen(SD_MOUNT_PATH));

    if (n == 0) {
      if (!hdMode) {
        xui::message(title, "No folders or disk images here.\n(.img .ima .dsk .vfd .flp)");
        return false;
      }
      strcpy(items.get(), "(empty - F7 creates a new hard disk)");
    }

    int key = 0;
    if (n) {
      xui::setMenuReserve(5);
      xui::setMenuInfo(fmFileInfo);
    }
    int s = xui::menuEx(title, items.get(), sel < n ? sel : 0, 44, &key,
                        hdMode ? "~Enter~ Use  ~F2~ Ren  ~F4~ Prot  ~F5~ Copy  ~F7~ New HD  ~F8~ Eject  ~Del~ Del  ~Esc~ Back"
                               : "~Enter~ Mount  ~F2~ Ren  ~F4~ Prot  ~F5~ Copy  ~F7~ Folder  ~F8~ Eject  ~Del~ Del  ~Esc~ Back");
    xui::setMenuReserve(0);
    xui::setMenuInfo(nullptr);
    if (s < 0 || key == (int) VirtualKey::VK_ESCAPE)
      return false;
    sel = s;

    // ejetar (disquete): nao depende do item
    if (key == (int) VirtualKey::VK_F8) {
      if (!hdMode) {
        machine->setDriveImage(drive, nullptr);
        return true;
      }
      // HD: o proximo boot fica sem HD (so disquete)
      if (xui::confirm("Hard disk", "Remove the hard disk? The emulator restarts now without HD.", false)) {
        preferences.putString("hd", "*none*");
        restartEmulator();
      }
      continue;
    }

    // novo HD (F7 no modo HD)
    if (hdMode && key == (int) VirtualKey::VK_F7) {
      // acima de 32 MB o DOS 3.x precisa de mais de uma particao (FDISK: primaria + estendida)
      static const int GEOM[][3] = { { 306, 4, 17 }, { 615, 4, 17 }, { 940, 4, 17 }, { 963, 6, 17 }, { 963, 8, 17 }, { 963, 10, 17 } };
      int t = xui::menu("New hard disk", "10 MB  (306/4/17);20 MB  (615/4/17);32 MB  (940/4/17);48 MB  (963/6/17);64 MB  (963/8/17);80 MB  (963/10/17)", 2);
      if (t < 0)
        continue;
      char nm[64] = "NEWHD";
      if (!xui::input("New hard disk", "Image name (.IMG is added)", nm, 55) || !*nm)
        continue;
      if (!fixImageName(nm, sizeof(nm), ".IMG", "New hard disk"))
        continue;
      if (fb.exists(nm, false)) {
        xui::message("New hard disk", "A file with this name already exists!", true);
        continue;
      }
      auto saved = xui::saveScreen();
      xui::progressBegin("New hard disk", "Creating... (then use FDISK and FORMAT)");
      bool ok = createHDImage(fb.directory(), nm, GEOM[t][0], GEOM[t][1], GEOM[t][2]);
      xui::restoreScreen(saved);
      if (!ok)
        xui::message("New hard disk", "Unable to create the image (SD card full?)", true);
      else {
        char desc[60] = "";
        if (askHDDesc(nm, desc, sizeof(desc)))
          mconfSetHD(nm, nullptr, desc, false, GEOM[t][0], GEOM[t][1], GEOM[t][2]);
      }
      fb.reload();
      continue;
    }

    if (n == 0)
      continue;

    // copia os dados do item: a lista do FileBrowser muda depois de renomear/apagar
    auto it = fb.get(map[s]);
    char name[MAXPATH];
    snprintf(name, sizeof(name), "%s", it->name);
    bool isDir = it->isDir;
    char path[MAXPATH];
    relativePath(fb, name, path, sizeof(path));
    bool isParent = strcmp(name, "..") == 0;
    bool prot = !isDir && isProtected(fb.directory(), name);
    char full[MAXPATH + 16];
    snprintf(full, sizeof(full), "%s/%s", fb.directory(), name);

    if (key == (int) VirtualKey::VK_RETURN) {

      if (isDir) {
        fb.changeDirectory(name);
        sel = 0;
      } else if (hdMode) {
        if (inUse(path, true)) {
          xui::message("Hard disk", "This hard disk is already in use.");
          continue;
        }
        if (xui::confirm("Hard disk", "Use this hard disk? The emulator restarts now.", false)) {
          preferences.putString("hd", name);
          restartEmulator();
        }
      } else {
        machine->setDriveImage(drive, path);
        return true;
      }

    } else if (key == (int) VirtualKey::VK_F4 && !isDir) {

      setProtected(fb.directory(), name, !prot);

    } else if ((key == (int) VirtualKey::VK_F2 || key == (int) VirtualKey::VK_DELETE) && !isParent && prot) {

      xui::message(name, "Protected (F4 to unprotect).", true);

    } else if ((key == (int) VirtualKey::VK_F2 || key == (int) VirtualKey::VK_DELETE) && hdMode && inUse(path, true)) {

      xui::message(name, "This hard disk is in use. Choose another one first.", true);

    } else if (key == (int) VirtualKey::VK_F2 && !isParent) {

      char newName[64];
      auto oext = strrchr(name, '.');
      if (isDir)
        snprintf(newName, sizeof(newName), "%s", name);
      else
        baseName(name, newName, sizeof(newName));   // edita so o nome; a extensao volta no fim
      bool okName = xui::input("Rename", name, newName, 55) && strlen(newName) > 0;
      if (okName && !isDir && !fixImageName(newName, sizeof(newName), oext ? oext : ".IMG", "Rename"))
        okName = false;
      if (okName && hdMode && strcmp(newName, name) == 0) {
        // mesmo nome: so a descricao
        char desc[60];
        mconfGetDesc(name, desc, sizeof(desc));
        if (askHDDesc(name, desc, sizeof(desc)))
          mconfSetHD(name, nullptr, desc, false);
      } else if (okName && strcmp(newName, name)) {
        if (fb.exists(newName, false))
          xui::message("Rename", "A file with this name already exists!", true);
        else {
          unmountIfMounted(path);
          fb.rename(name, newName);
          if (hdMode) {
            char c1[80], c2[80];
            chsName(name, c1, sizeof(c1));
            chsName(newName, c2, sizeof(c2));
            if (fb.exists(c1, false))
              fb.rename(c1, c2);
            if (String(preferences.getString("hd", "")).equalsIgnoreCase(name))
              preferences.putString("hd", newName);
            char desc[60];
            mconfGetDesc(name, desc, sizeof(desc));
            mconfSetHD(name, newName, nullptr, false);
            if (askHDDesc(newName, desc, sizeof(desc)))
              mconfSetHD(newName, nullptr, desc, false);
          }
          fb.reload();   // atualiza a lista
        }
      }

    } else if (key == (int) VirtualKey::VK_F5 && !isDir) {

      char newName[64];
      auto ext = strrchr(name, '.');
      snprintf(newName, sizeof(newName), "%.*s_2", (int)(ext ? ext - name : strlen(name)), name);
      if (xui::input("Duplicate", name, newName, 55) && strlen(newName) > 0 &&
          fixImageName(newName, sizeof(newName), ext ? ext : ".IMG", "Duplicate") && strcasecmp(newName, name)) {
        if (fb.exists(newName, false))
          xui::message("Duplicate", "A file with this name already exists!", true);
        else {
          char dst[MAXPATH + 16];
          snprintf(dst, sizeof(dst), "%s/%s", fb.directory(), newName);
          auto saved = xui::saveScreen();
          char msg[96];
          snprintf(msg, sizeof(msg), "Copying %s ...", name);
          xui::progressBegin("Duplicate", msg);
          bool ok = copyFile(full, dst);
          xui::restoreScreen(saved);
          if (!ok)
            xui::message("Duplicate", "Unable to copy (SD card full?)", true);
          else if (hdMode) {
            char desc[60];
            mconfGetDesc(name, desc, sizeof(desc));
            if (*desc && strlen(desc) + 7 < sizeof(desc))
              strcat(desc, " (copy)");
            if (askHDDesc(newName, desc, sizeof(desc)))
              mconfSetHD(newName, nullptr, desc, false);
            char c1[80], c2[80], f1[MAXPATH + 16], f2[MAXPATH + 16];
            chsName(name, c1, sizeof(c1));
            chsName(newName, c2, sizeof(c2));
            snprintf(f1, sizeof(f1), "%s/%s", fb.directory(), c1);
            snprintf(f2, sizeof(f2), "%s/%s", fb.directory(), c2);
            if (fb.exists(c1, false)) {
              xui::progressBegin("Duplicate", "Copying geometry...");
              copyFile(f1, f2);
            }
          }
          fb.reload();
        }
      }

    } else if (key == (int) VirtualKey::VK_DELETE && !isParent) {

      char msg[MAXPATH + 16];
      snprintf(msg, sizeof(msg), "Delete %s?", name);
      if (xui::confirm("Delete", msg, false)) {
        unmountIfMounted(path);
        fb.remove(name);
        if (hdMode) {
          char c1[80];
          chsName(name, c1, sizeof(c1));
          if (fb.exists(c1, false))
            fb.remove(c1);
          mconfSetHD(name, nullptr, nullptr, true);
        }
        fb.reload();   // atualiza a lista antes de conferir
        if (fb.exists(name, false))
          xui::message("Delete", isDir ? "Unable to delete: the folder is not empty." : "Unable to delete the file.", true);
      }

    } else if (key == (int) VirtualKey::VK_F7 && !hdMode) {

      char dirName[64] = "";
      if (xui::input("New folder", "Folder name", dirName, 60) && strlen(dirName) > 0) {
        if (fb.exists(dirName, false))
          xui::message("New folder", "A file or folder with this name already exists!", true);
        else {
          fb.makeDirectory(dirName);
          fb.reload();
        }
      }

    }
  }
}


// cria um disquete vazio (FAT12 formatado) em \DISK e monta no drive
static void createEmptyDisk(int drive)
{
  // tipos de createFATFloppyImage(): 1 = 360K, 2 = 720K, 3 = 1.2M, 4 = 1.44M
  static const int DISKTYPE[] = { 1, 2, 3, 4 };
  int t = xui::menu("Create empty disk", "360 KB  (5.25\" DD);720 KB  (3.5\" DD);1.2 MB  (5.25\" HD);1.44 MB (3.5\" HD)", 0);
  if (t < 0)
    return;

  char name[64] = "NEWDISK";
  if (!xui::input("Create empty disk", "Image name (.IMG is added)", name, 55) || strlen(name) == 0)
    return;
  if (!fixImageName(name, sizeof(name), ".IMG", "Create empty disk"))
    return;
  char path[80];
  snprintf(path, sizeof(path), DISK_DIR "/%s", name);

  FileBrowser fb(DISK_ROOT);
  if (fb.exists(name, false)) {
    if (!xui::confirm("Create empty disk", "File already exists. Overwrite?", false))
      return;
    unmountIfMounted(path);
  }

  auto saved = xui::saveScreen();
  char msg[96];
  snprintf(msg, sizeof(msg), "Creating %s ...", name);
  xui::progressBegin("Create empty disk", msg);
  bool ok = createFATFloppyImage(DISKTYPE[t], DISK_ROOT, name, [](int percent) {
    xui::progressUpdate(percent);
    return true;
  });
  xui::restoreScreen(saved);

  if (ok)
    machine->setDriveImage(drive, path);
  else
    xui::message("Create empty disk", "Unable to create the disk image!", true);
}


// submenu de um drive de disquete (0 = A, 1 = B)
static void floppyMenu(int drive)
{
  char title[32];
  snprintf(title, sizeof(title), "Floppy %c:", 'A' + drive);
  bool mounted = machine->disk(drive) != nullptr;
  int s = xui::menu(title, mounted ? "Select disk image...;Create empty disk...;Eject disk" : "Select disk image...;Create empty disk...");
  switch (s) {
    case 0:
      fileManager(drive);
      break;
    case 1:
      createEmptyDisk(drive);
      break;
    case 2:
      machine->setDriveImage(drive, nullptr);
      break;
  }
}


// reinicia o ESP32 deixando o mouse "quieto": o mouse nao perde a energia no reinicio e, se continuar
// mandando movimento, a deteccao no proximo boot falha (era preciso resetar a placa varias vezes)
static void restartEmulator()
{
  // cada etapa vai para a serial (115200): se travar, a ultima linha mostra onde
  printf("restart: inicio\n");
  machine->pauseSound(true);
  auto mouse = PS2Controller::mouse();
  if (mouse && mouse->isMouseAvailable()) {
    mouse->sendCommand(0xF5, 0xFA);   // desliga o envio de dados
    delay(50);
  }
  printf("restart: mouse ok\n");
  // fecha as imagens de disco e desmonta o SD: o cartao fica num estado limpo para o proximo boot
  for (int i = 0; i < DISKCOUNT; ++i)
    machine->setDriveImage(i, nullptr);
  printf("restart: discos fechados\n");
  FileBrowser::unmountSDCard();
  printf("restart: SD desmontado\n");
  xui::clear(0x00, ' ');
  machine->graphicsAdapter()->enableVideo(false);
  delay(100);
  printf("restart: reset do sistema\n");
  fflush(stdout);
  delay(20);
  // reset completo do chip (CPU e perifericos, como o botao da placa), sem depender das tarefas que estao rodando:
  // o esp_restart() precisa parar o outro nucleo e pode ficar preso com a AdLib/EGA ativas
  SET_PERI_REG_MASK(RTC_CNTL_OPTIONS0_REG, RTC_CNTL_SW_SYS_RST);
  while (true)
    ;
}


// nome curto do HD em uso (sem pasta)
static void hdLabel(char * out, int maxlen)
{
  auto f = machine->diskFilename(2);
  if (!f) {
    snprintf(out, maxlen, "(none)");
    return;
  }
  auto p = strrchr(f, '/');
  // descricao do mconfs.txt; sem descricao, o nome do arquivo
  char desc[60];
  mconfGetDesc(p ? p + 1 : f, desc, sizeof(desc));
  snprintf(out, maxlen, "%s", *desc ? desc : (p ? p + 1 : f));
  for (char * c = out; *c; ++c)
    if (*c == ';' || *c == '\t')
      *c = ',';
  // cabe ao lado do rotulo (menu de 42 colunas): corta e marca com >>
  constexpr int MAXW = 25;
  if ((int) strlen(out) > MAXW) {
    out[MAXW - 1] = '\xAF';
    out[MAXW] = 0;
  }
}


// confirma e reinicia o ESP32 para aplicar placa de video / som
static void askRestart(char const * key, int value)
{
  if (xui::confirm("Restart required", "This change requires restarting the emulator.\nRestart now?", false)) {
    preferences.putInt(key, value);
    restartEmulator();
  }
}


// menu de setup: ALT + PRINTSCREEN ou CTRL + ALT + UP
void sysReqCallback()
{
  machine->pauseSound(true);
  xui::open();

  enum { M_CONTINUE, M_SEP1, M_FDA, M_FDB, M_HD, M_SEP2, M_MACHINE, M_RAM, M_VIDEO, M_SOUND, M_KBD, M_MPORT, M_MONITOR, M_TIMERSYNC, M_SPKMODE, M_DEBUG, M_SEP3, M_RESETMACHINE, M_RESTART };

  int sel = 0;
  for (bool loop = true; loop; ) {

    drawDesktop();

    char fda[24], fdb[24], hd[32];
    hdLabel(hd, sizeof(hd));
    diskLabel(0, fda, sizeof(fda));
    diskLabel(1, fdb, sizeof(fdb));
    int monitor = (int) machine->graphicsAdapter()->monitorMode();

    char items[400];
    snprintf(items, sizeof(items),
             "Continue;-;"
             "Floppy A:\t%s;Floppy B:\t%s;Hard disk\t%s;-;"
             "Machine\t%s;Memory\t%dK;Video card\t%s;Sound\t%s;Keyboard\t%s;Mouse port\t%s;Monitor\t%s;Timer sync\t%s;PC speaker\t%s;Debug\t%s;-;"
             "Restart machine;Restart emulator",
             fda, fdb, hd, MACHINENAMES[s_machine], machine->ramKB(),
             VIDEONAMES[machine->videoCard()], SOUNDNAMES[machine->adlib()], KBDNAMES[machine->kbdLayout()], MPORTNAMES[s_mousePortMode == 0 ? (s_mousePortSerial ? 1 : 0) : s_mousePortMode + 1], MONITORNAMES[monitor & 3], machine->timerSync() ? "On" : "Off", SPKMODES[machine->speakerMode()], s_debug ? "On" : "Off");

    xui::setRefreshOnMonitor(true);
    xui::setMenuInfo(setupInfo);
    sel = xui::menu("Setup", items, sel, 42);
    xui::setMenuInfo(nullptr);   // 42: o quadro de Debug cabe a esquerda
    xui::setRefreshOnMonitor(false);
    if (sel == -2) {           // CTRL+ALT+DOWN: monitor mudou, redesenha o menu no mesmo item
      sel = xui::lastSelected();
      continue;
    }

    switch (sel) {

      case M_HD:
        fileManager(2);
        break;

      // maquina: Enter alterna o perfil (titulo ja muda; a tela de boot no proximo inicio)
      case M_MACHINE:
        s_machine = (s_machine + 1) % 2;
        preferences.putInt("machine", s_machine);
        xui::setTitle(MACHINETITLES[s_machine]);
        setBootMessageLanguage();
        break;

      case M_FDA:
      case M_FDB:
        floppyMenu(sel - M_FDA);
        break;

      // memoria: 704K so com CGA ou Hercules (a EGA usa A0000-AFFFF)
      case M_RAM:
      {
        int cur = 2;
        for (int i = 0; i < 4; ++i)
          if (RAMSIZES[i] == machine->ramKB())
            cur = i;
        int v = xui::menu("Memory", machine->videoCard() >= 2 ? "256K;512K;640K" : "256K;512K;640K;704K (CGA/Hercules)", cur);
        if (v > -1 && v != cur)
          askRestart("ram", RAMSIZES[v]);
        break;
      }

      case M_VIDEO:
      {
        int v = xui::menu("Video card", "CGA;Hercules;EGA;EGA 640x350 (experimental)", machine->videoCard());
        if (v > -1 && v != machine->videoCard())
          askRestart("video", v);
        break;
      }

      case M_SOUND:
      {
        int v = xui::menu("Sound", "Speaker;Speaker + AdLib", machine->adlib());
        if (v > -1 && v != (int) machine->adlib())
          askRestart("sound", v);
        break;
      }

      // teclado: Enter alterna US / ABNT2 (como o monitor)
      case M_KBD:
      {
        int v = machine->kbdLayout() ^ 1;
        machine->setKbdLayout(v);
        preferences.putInt("kbd", v);
        break;
      }

      // monitor: aplica na hora (o proprio menu muda de cor)
      case M_MONITOR:
        machine->graphicsAdapter()->cycleMonitorMode();
        break;

      // porta do mouse: mouse PS/2 ou serial COM1 (modem WiFi). Muda no proximo inicio
      case M_MPORT:
      {
        int v = xui::menu("Mouse port", "Auto (mouse or modem);PS/2 mouse;Serial COM1 (WiFi modem)", s_mousePortMode);
        if (v > -1 && v != s_mousePortMode)
          askRestart("mport", v);
        break;
      }

      // debug: Enter liga/desliga o quadro de medidas (fica salvo)
      // timer sync: entrega depois os IRQ0 que chegaram com o anterior ainda em atendimento
      //   On (padrao): musicas guiadas pelo timer ficam certas (ex. Digger)
      //   Off: como no XT real, os ticks perdidos se perdem (jogos que medem tempo pelos ticks, ex. Arkanoid II)
      case M_TIMERSYNC:
        machine->setTimerSync(!machine->timerSync());
        preferences.putInt("tsync", machine->timerSync());
        break;

      // PC speaker: PCM (amostras, segue cada pulso) ou Classic (mede a frequencia e toca uma onda)
      case M_SPKMODE:
        machine->setSpeakerMode((machine->speakerMode() + 1) % 3);
        preferences.putInt("spkmode", machine->speakerMode());
        break;

      case M_DEBUG:
        s_debug = !s_debug;
        preferences.putInt("debug", s_debug);
        break;

      case M_RESETMACHINE:
        if (xui::confirm("Restart machine", "Restart the emulated PC (like CTRL+ALT+DEL)?", false)) {
          machine->trigReset();
          loop = false;
        }
        break;

      case M_RESTART:
        if (xui::confirm("Restart emulator", "Restart the ESP32?", false))
          restartEmulator();
        break;

      // Continue / Esc
      default:
        loop = false;
        break;
    }
  }

  xui::close();
  machine->pauseSound(false);
}



////////////////////////////////////////////////////////////////////////////////////////////////////
// inicializacao


// erro fatal na inicializacao: mensagem em texto a partir da linha 4, coluna 1, e para
static void fatalError(char const * text)
{
  int row = 3;
  char const * p = text;
  while (p && *p) {
    char const * end = strchr(p, '\n');
    int len = end ? end - p : strlen(p);
    char line[81];
    snprintf(line, sizeof(line), "%.*s", len > 80 ? 80 : len, p);
    xui::print(0, row++, line, 0x07);
    p = end ? end + 1 : nullptr;
  }
  printf("ERROR: %s\n", text);
  while (true)
    delay(1000);
}


// geometria de um HD em \HD: arquivo "nome.chs" ao lado (texto "cil,cab,set"), ou a da linha do mconfs.txt
// que usa o mesmo arquivo; senao fica 0,0,0 e o emulador detecta pela imagem (como no PCEmulator original)
static void hdGeometry(char const * path, MachineConfItem * entry, uint16_t * c, uint16_t * h, uint16_t * s)
{
  *c = *h = *s = 0;
  char chs[128];
  snprintf(chs, sizeof(chs), SD_MOUNT_PATH "/%s", path);
  auto dot = strrchr(chs, '.');
  if (dot && strlen(dot) >= 4) {
    strcpy(dot, ".chs");
    if (auto f = fopen(chs, "r")) {
      unsigned a = 0, b = 0, d = 0;
      int n = fscanf(f, "%u,%u,%u", &a, &b, &d);
      fclose(f);
      if (n == 3 && a && b && d) {
        *c = a; *h = b; *s = d;
        return;
      }
    }
  }
  if (entry) {
    *c = entry->cylinders[2]; *h = entry->heads[2]; *s = entry->sectors[2];
  }
}


// linha do mconfs.txt cujo HD e o arquivo 'name' (so o nome, sem pasta)
static MachineConfItem * confForHD(MachineConf & mconf, char const * name)
{
  for (int i = 0; ; ++i) {
    auto it = mconf.getItem(i);
    if (!it)
      return nullptr;
    if (it->disk[2]) {
      auto base = strrchr(it->disk[2], '/');
      if (strcasecmp(base ? base + 1 : it->disk[2], name) == 0)
        return it;
    }
  }
}


// bipe curto do POST: tocado pelo gerador de som da propria maquina, assim que ela comeca a rodar
// (um segundo gerador de som temporario aqui atrapalhava o video no boot)
static void postBeep()
{
  // depois de um esp_restart() o I2S e o DAC nao sao zerados: o DAC continua ligado ao I2S
  // (som da maquina anterior) e o gerador de onda nao chega na saida. Desliga isso primeiro.
  periph_module_reset(PERIPH_I2S0_MODULE);
  CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL1_REG, SENS_DAC_DIG_FORCE);
  CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL1_REG, SENS_DAC_CLK_INV);
  // onda quadrada por hardware (LEDC) no GPIO 25: tom do PC real (timer 1331 = 896 Hz), com o "brilho" dos harmonicos
  //   pulso de 12%: volume parecido com o do speaker emulado
  // o reset pelo menu (reset do sistema) nao zera o dominio RTC: o pino 25 continua como saida do DAC
  // e o LEDC (matriz de GPIO) nao chega nele. Devolve o pino ao modo digital.
  dac_output_disable(DAC_CHANNEL_1);
  rtc_gpio_deinit(GPIO_NUM_25);
  pinMode(25, OUTPUT);
  constexpr int CH = 7;
  ledcSetup(CH, 896, 8);
  ledcAttachPin(25, CH);
  ledcWrite(CH, 31);
  delay(150);
  ledcWrite(CH, 0);
  ledcDetachPin(25);
  pinMode(25, INPUT);      // o gerador de som da maquina assume o pino (DAC) depois
}


// memoria no boot, terminando com o bipe
//   Cobra XPC: linha 4, conta "000K".."640K" de 64K em 64K (640ms por passo: cerca de 7 segundos) e "640K OK"
//   IBM PC XT: tela preta por 7 segundos (o "teste de memoria") e entao "640 KB OK" na linha 1
static void memoryCount()
{
  char line[24];
  int total = machine->ramKB();
  if (s_machine == 1) {
    // IBM PC XT: conta de 16K em 16K na linha 1 (cerca de 7 segundos para 640K) e "640 KB OK"
    int steps = total / 16;
    for (int kb = 0; kb <= total; kb += 16) {
      snprintf(line, sizeof(line), "%d KB", kb);
      xui::print(0, 0, line, 0x07);
      delay(kb < total ? 7000 / steps : 0);
    }
    snprintf(line, sizeof(line), "%d KB OK", total);
    xui::print(0, 0, line, 0x07);
    postBeep();
    delay(2000);
    return;
  }
  for (int kb = 0; kb <= total; kb += 64) {
    snprintf(line, sizeof(line), "%03dK", kb);
    xui::print(0, 3, line, 0x07);
    delay(kb < total ? 640 : 0);
  }
  snprintf(line, sizeof(line), "%03dK OK", total);
  xui::print(0, 3, line, 0x07);
  postBeep();
  delay(2000);
}


void setup()
{
  Serial.begin(115200); delay(500); printf("\n\n\nXPC Cobra\n\n");

  disableCore0WDT();
  delay(100); // experienced crashes without this delay!
  disableCore1WDT();

  preferences.begin("PCEmulator", false);

  // uncomment to clear preferences
  //preferences.clear();

  // teclado (e mouse opcional) - teclas virtuais para a interface em texto
  // XPC: sem a deteccao "rapida": o mouse ganha ate 3 tentativas de reset (resolve mouse nao reconhecido
  //      depois de reiniciar o emulador)
  // porta do mouse: mouse PS/2, ou serial (COM1) para o modem WiFi ligado direto (TX = GPIO 26, RX = GPIO 27)
  //   Auto (padrao): procura um mouse; sem mouse, a porta vira serial (modem)
  s_mousePortMode = preferences.getInt("mport", 0);
  if (s_mousePortMode < 0 || s_mousePortMode > 2)
    s_mousePortMode = 0;
  s_mousePortSerial = (s_mousePortMode == 2);
  PS2Controller::begin(s_mousePortSerial ? PS2Preset::KeyboardPort0 : PS2Preset::KeyboardPort0_MousePort1, KbdMode::GenerateVirtualKeys);
  if (s_mousePortMode == 0 && !(PS2Controller::mouse() && PS2Controller::mouse()->isMouseAvailable())) {
    s_mousePortSerial = true;
    PS2Controller::begin(PS2Preset::KeyboardPort0, KbdMode::GenerateVirtualKeys);
    // o programa do ULP (coprocessador que le o PS/2) continua com a porta 1 ligada depois do primeiro
    // begin(): ele passaria a "ler" os dados da serial nos pinos 26/27 como se fossem do mouse, e
    // atrapalharia o teclado (porta 0) ate travar tudo. Desliga a recepcao da porta 1 de vez.
    PS2Controller::disableRX(1);
  }

  // a maquina e criada ja aqui: a interface em texto usa o adaptador de video dela
  machine = new Machine;
  xui::begin(machine->graphicsAdapter());
  xui::open();
  // perfil da maquina (so apresentacao)
  s_machine = preferences.getInt("machine", 0) == 1 ? 1 : 0;
  xui::setTitle(MACHINETITLES[s_machine]);
  // tela de inicio: tudo preto. Cobra XPC: nome na linha 1, coluna 1. IBM PC XT: nada (so a contagem)
  xui::clear(0x07, ' ');
  if (s_machine == 0)
    xui::print(0, 0, MACHINETITLES[0], 0x07);

  // we need PSRAM for this app, but we will handle it manually, so please DO NOT enable PSRAM on your development env
  #ifdef BOARD_HAS_PSRAM
  fatalError("Please disable PSRAM in the Arduino settings (Tools > PSRAM > Disabled).");
  #endif

  // note: we use just 2MB of PSRAM so the infamous PSRAM bug should not happen. But to avoid gcc compiler hack (-mfix-esp32-psram-cache-issue)
  // we enable PSRAM at runtime, otherwise the hack slows down CPU too much (PSRAM_HACK is no more required).
  if (esp_spiram_init() != ESP_OK)
    fatalError("This app requires a board with PSRAM!");

  #ifndef BOARD_HAS_PSRAM
  esp_spiram_init_cache();
  #endif

  if (!FileBrowser::mountSDCard(false, SD_MOUNT_PATH, 8))   // @TODO: reduce to 4?
    fatalError("SD card not found! Insert the card and restart.");

  esp_register_shutdown_handler(shutdownHandler);

  updateDateTime();

  // configuracao da maquina: sempre a primeira linha do mconfs.txt
  MachineConf mconf;
  loadMachineConfiguration(&mconf);
  auto conf = mconf.getItem(0);
  if (!conf)
    fatalError("mconfs.txt has no machine configuration!");
  snprintf(s_confDesc, sizeof(s_confDesc), "%s", conf->desc);

  char msg[160];

  // pastas \HD e \DISK (criadas se nao existirem)
  {
    FileBrowser fb(SD_MOUNT_PATH);
    if (!fb.exists(HD_DIR, false))
      fb.makeDirectory(HD_DIR);
    if (!fb.exists(DISK_DIR, false))
      fb.makeDirectory(DISK_DIR);
  }

  char const * diskFilename[DISKCOUNT];
  for (int i = 0; i < DISKCOUNT; ++i)
    diskFilename[i] = getDisk(conf->disk[i]);

  // HD: escolhido no Setup (pasta \HD), ou o do mconfs.txt (na raiz ou movido para \HD)
  static char hdPath[96];
  uint16_t hdC = conf->cylinders[2], hdH = conf->heads[2], hdS = conf->sectors[2];
  String hdPref = preferences.getString("hd", "");
  if (hdPref == "*none*") {
    diskFilename[2] = nullptr;      // HD ejetado no Setup
    if (conf->bootDrive >= 2)
      conf->bootDrive = 0;
  } else if (hdPref.length()) {
    snprintf(hdPath, sizeof(hdPath), HD_DIR "/%s", hdPref.c_str());
    if (getDisk(hdPath)) {
      diskFilename[2] = hdPath;
      auto entry = confForHD(mconf, hdPref.c_str());
      hdGeometry(hdPath, entry, &hdC, &hdH, &hdS);
      // descricao na barra do Setup: a da linha do mconfs.txt com esse HD, ou o nome do arquivo
      snprintf(s_confDesc, sizeof(s_confDesc), "%s", entry && entry->desc ? entry->desc : hdPref.c_str());
      if (conf->bootDrive != 0x80 && conf->bootDrive != 2 && !diskFilename[0])
        conf->bootDrive = 2;
    }
  } else if (!diskFilename[2] && conf->disk[2]) {
    auto base = strrchr(conf->disk[2], '/');
    snprintf(hdPath, sizeof(hdPath), HD_DIR "/%s", base ? base + 1 : conf->disk[2]);
    if (getDisk(hdPath))
      diskFilename[2] = hdPath;
  }

  // sem disco de boot nao para mais: a BIOS mostra "No boot disk" e o usuario escolhe no Setup
  //   (Floppy A: / Hard disk) e tecla para tentar de novo
  // without WiFi it is possible to increase SD card speed
  FileBrowser::setSDCardMaxFreqKHz(SDMMC_FREQ_DEFAULT);
  FileBrowser::remountSDCard();

  machine->setBaseDirectory(SD_MOUNT_PATH);
  for (int i = 0; i < DISKCOUNT; ++i) {
    if (i == 2)
      machine->setDriveImage(i, diskFilename[i], hdC, hdH, hdS);
    else
      machine->setDriveImage(i, diskFilename[i], conf->cylinders[i], conf->heads[i], conf->sectors[i]);
  }

  machine->setBootDrive(conf->bootDrive);

  // configuracoes do XPC (padrao: CGA / Speaker / US)
  int video = preferences.getInt("video", 0);
  machine->setVideoCard(video >= 0 && video <= 3 ? video : 0);
  machine->setAdLib(preferences.getInt("sound", 0) == 1);
  machine->setKbdLayout(preferences.getInt("kbd", 0) == 1 ? 1 : 0);
  s_debug = preferences.getInt("debug", 0) == 1;
  machine->setTimerSync(preferences.getInt("tsync", 1) == 1);
  { int m = preferences.getInt("spkmode", 0); machine->setSpeakerMode(m >= 0 && m <= 2 ? m : 0); }
  int ram = preferences.getInt("ram", 640);
  machine->setRamKB(ram == 256 || ram == 512 || ram == 704 ? ram : 640);

  auto serial1 = new SerialPort;
  if (s_mousePortSerial) {
    // os pinos 26/27 podem ter ficado no modo RTC do PS/2 (deteccao do mouse no modo Auto, ou do boot
    // anterior: o reset pelo menu nao zera o dominio RTC). Devolve-os ao modo digital para a UART.
    rtc_gpio_deinit(GPIO_NUM_26);
    rtc_gpio_deinit(GPIO_NUM_27);
    serial1->setSignals(27, 26);
  }          // RX = GPIO 27 (PS/2 pino 1), TX = GPIO 26 (PS/2 pino 5)
  else
    serial1->setSignals(UART_URX, UART_UTX);
  machine->setCOM1(serial1);

  machine->setSysReqCallback(sysReqCallback);
  setBootMessageLanguage();

  // contagem de memoria como nos PCs da epoca, com um tique no speaker a cada passo
  memoryCount();

  // tela preta e video desligado ate a BIOS ligar o modo de video (sem "sujeira" na troca)
  xui::clear(0x00, ' ');
  machine->graphicsAdapter()->enableVideo(false);
  xui::close();

  machine->run();
}



void loop()
{
  vTaskDelete(NULL);
}
