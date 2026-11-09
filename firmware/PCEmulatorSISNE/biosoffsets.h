// gerado a partir de bios.lst (nasm): posicoes dentro da ROM da BIOS
#pragma once
#define BIOSROM_EBDASEG_OFF  0x25df   // segmento da EBDA
#define BIOSROM_BOOTMSGPT_OFF 0x25e1  // 1 = mensagem "sem disco de boot" em portugues
#define BIOSROM_MEMSIZE_OFF  0x25f5   // memoria em KB (INT 12h, copiado para 40:13)
