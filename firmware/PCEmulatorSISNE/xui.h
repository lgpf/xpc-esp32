// XPC Cobra - interface em modo texto 80x25 (estilo CheckIt / Norton)
//
// Desenha num buffer proprio de 4000 bytes (caractere + atributo, como B800:0000) e
// mostra pelo proprio adaptador de video do emulador, sem trocar o modo VGA.
// O teclado e lido direto do controlador PS/2 (o programa emulado nao recebe nada).

#pragma once

#include "fabgl.h"
#include "xgraphicsadapter.h"

#include <functional>


namespace xui {


// atributos (fundo << 4 | frente), cores CGA
enum : uint8_t {
  A_DESKTOP   = 0x17,   // fundo da area de trabalho (cinza sobre azul)
  A_BAR       = 0x70,   // barras de titulo e de ajuda (preto sobre cinza)
  A_BARKEY    = 0x74,   // tecla destacada na barra de ajuda (vermelho sobre cinza)
  A_WINDOW    = 0x1F,   // janela (branco sobre azul)
  A_FRAME     = 0x1B,   // moldura (ciano claro sobre azul)
  A_TITLE     = 0x1E,   // titulo da janela (amarelo sobre azul)
  A_SELECTED  = 0x70,   // item selecionado (preto sobre cinza: contraste bom tambem nos monitores mono)
  A_DISABLED  = 0x17,   // separador / item inativo
  A_VALUE     = 0x1E,   // valor de um item (amarelo sobre azul)
  A_INPUT     = 0x70,   // campo de texto
  A_BUTTON    = 0x1F,   // botao sem foco (como a janela)
  A_BUTTONSEL = 0x70,   // botao com foco (igual ao item selecionado, e entre < >)
  A_ERROR     = 0x4F,   // janela de erro (branco sobre vermelho)
  A_ERRFRAME  = 0x4E,
  A_SHADOW    = 0x08,
};


// liga a interface ao adaptador de video do emulador
void begin(xpc::GraphicsAdapter * adapter);

// mostra a tela do menu (salva o estado do video) / volta para a tela do programa emulado
void open();
void close();
bool isOpen();

// desenho
void clear(uint8_t attr = A_DESKTOP, uint8_t ch = 0xB0);
void putChar(int x, int y, uint8_t ch, uint8_t attr);
void setAttr(int x, int y, int w, uint8_t attr);
void print(int x, int y, char const * text, uint8_t attr, int maxw = 80);
void printCenter(int y, char const * text, uint8_t attr);
void fill(int x, int y, int w, int h, uint8_t ch, uint8_t attr);
void box(int x, int y, int w, int h, uint8_t frameAttr, uint8_t fillAttr, char const * title = nullptr, uint8_t titleAttr = A_TITLE, bool shadow = true);

// area de trabalho: barra de titulo, fundo e barra de ajuda
void desktop(char const * rightText = nullptr);
void setTitle(char const * title);   // nome da maquina na barra de titulo
void helpBar(char const * text);   // trechos entre ~ ficam destacados: "~Enter~ OK"

// salva / restaura a tela inteira (para janelas sobrepostas)
uint8_t * saveScreen();
void restoreScreen(uint8_t * saved);   // tambem libera

// teclado (so eventos de tecla pressionada)
void getKey(VirtualKeyItem * item);
// CTRL+ALT+DOWN (monitor) dentro de um menu: com 'value' ligado o menu retorna -2 para ser redesenhado;
// lastSelected() devolve o item que estava selecionado
void setRefreshOnMonitor(bool value);
int lastSelected();
// funcao chamada a cada desenho do proximo menu (item selecionado), p.ex. para um quadro de descricao
void setMenuInfo(void (*callback)(int selected));
// linhas reservadas embaixo do proximo menu (o menu sobe e fica menor), p.ex. para um quadro de informacao
void setMenuReserve(int rows);

// componentes
//   menu: itens separados por ';'. "-" = separador. Um item pode ter "\t" + valor (mostrado a direita)
//   retorna o indice escolhido ou -1 (Esc)
int  menu(char const * title, char const * items, int selected = 0, int width = 0);
//   menuEx: como menu, mas F2, F7 e Del tambem saem (exitKey recebe a tecla, VirtualKey como int)
int  menuEx(char const * title, char const * items, int selected, int width, int * exitKey, char const * help);
bool confirm(char const * title, char const * message, bool defaultYes = false);
void message(char const * title, char const * message, bool error = false);
bool input(char const * title, char const * label, char * buf, int maxlen);

// caixa de progresso: desenhada uma vez com progressBegin e atualizada com progressUpdate
void progressBegin(char const * title, char const * message);
void progressUpdate(int percent);

// caixa simples sem esperar tecla (mensagens de inicializacao)
void info(char const * title, char const * message);


}  // namespace xui
