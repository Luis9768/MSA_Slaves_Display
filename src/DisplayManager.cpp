#include "DisplayManager.h"
#include "Config.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

LV_IMG_DECLARE(AIPLAN_LOGO_FINAL_2020);

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(TOUCH_XPT_CS, TOUCH_XPT_IRQ);

// Variável de controle de seleção
// 0 = Nenhuma seleção (Lista)
// >0 = ID do produto selecionado
// -1 = Solicitou voltar
// -3 = Confirmou Data
// -4 = Confirmou RE
volatile int produtoSelecionado = 0;

// Globais para Data e RE
lv_obj_t *rollerDia;
lv_obj_t *rollerMes;
lv_obj_t *rollerAno;
lv_obj_t *lblCRE; // Display do RE
int currentREVal = 0;
char reBuffer[10] = "";
lv_obj_t *lblDataDisplay; // [FIX] Declaration added

lv_obj_t *barProducao;
lv_obj_t *msgConclusao = NULL;
lv_obj_t *lblContador = NULL;
lv_obj_t *cursor_obj = NULL;

// Globais do Carousel
std::vector<Receita> currentLista;
int currentIndex = 0;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area,
                   lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

void my_touch_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();

    // Calibração Direta X->X, Y->Y (Tentativa 2)
    int16_t x = map(p.x, 200, 3900, 0, 240);
    int16_t y = map(p.y, 250, 3750, 0, 320);

    if (x < 0)
      x = 0;
    if (x >= 240)
      x = 239;
    if (y < 0)
      y = 0;
    if (y >= 320)
      y = 319;

    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;

    // Atualiza posição do cursor visual
    if (cursor_obj) {
      lv_obj_set_pos(cursor_obj, x - 5, y - 5); // Centraliza a bolinha
      lv_obj_clear_flag(cursor_obj, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(cursor_obj); // Garante que está no topo
    }

    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 200) {
      DBGF("TOUCH: Raw(%d,%d) -> Screen(%d,%d)\n", p.x, p.y, x, y);
      lastDebug = millis();
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void recriarDebugCursor() {
  cursor_obj = lv_obj_create(lv_scr_act());
  lv_obj_set_size(cursor_obj, 10, 10);
  lv_obj_set_style_bg_color(cursor_obj, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_set_style_radius(cursor_obj, LV_RADIUS_CIRCLE, 0);
  lv_obj_clear_flag(cursor_obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(cursor_obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cursor_obj);
}

// Handler genérico para o botão "Voltar"
void event_handler_btn_voltar_click(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    produtoSelecionado = -1;
  }
}

void proximoProdutoCarousel() {
  if (currentLista.empty())
    return;
  currentIndex++;
  if (currentIndex >= currentLista.size())
    currentIndex = 0;
  mostrarCarouselSlave(currentLista, currentIndex);
}

void entrarProdutoCarousel() {
  if (currentLista.empty())
    return;
  produtoSelecionado = currentLista[currentIndex].id;
}

void event_handler_proximo(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    proximoProdutoCarousel();
  }
}

void event_handler_entrar(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    entrarProdutoCarousel();
  }
}

void mostrarCarouselSlave(std::vector<Receita> lista, int indice) {
  currentLista = lista;
  if (!lista.empty()) {
    if (indice >= lista.size())
      indice = 0;
    if (indice < 0)
      indice = lista.size() - 1;
  }
  currentIndex = indice;

  lv_obj_clean(lv_scr_act());
  recriarDebugCursor();

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xF0F0F0), 0);

  if (!lista.empty()) {
    Receita r = lista[currentIndex];

    // --- CARTÃO DO PRODUTO ---
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, 230, 240);             // Quase tela toda
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -30); // Mais pra cima
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 0, 0);

    // Header do Cartão (Azul -> Red)
    lv_obj_t *cardHeader = lv_obj_create(card);
    lv_obj_set_size(cardHeader, 230, 45);
    lv_obj_align(cardHeader, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(cardHeader, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_radius(cardHeader, 0, 0); // Reto
    lv_obj_clear_flag(cardHeader, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lblID = lv_label_create(cardHeader);
    lv_label_set_text_fmt(lblID, "PRODUTO #%d", r.id);
    lv_obj_align(lblID, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(lblID, lv_color_white(), 0);
    lv_obj_set_style_text_font(lblID, &lv_font_montserrat_22, 0);

    // Descrição
    lv_obj_t *lblDesc = lv_label_create(card);
    lv_label_set_long_mode(lblDesc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lblDesc, 210);
    lv_label_set_text(lblDesc, r.descricao);
    lv_obj_align(lblDesc, LV_ALIGN_TOP_LEFT, 10, 55);
    lv_obj_set_style_text_font(lblDesc, &lv_font_montserrat_18,
                               0); // 18 (Requested)
    lv_obj_set_style_text_color(lblDesc, lv_color_black(), 0);

    // Box de Detalhes
    lv_obj_t *boxDet = lv_obj_create(card);
    lv_obj_set_size(boxDet, 210, 80);
    lv_obj_align(boxDet, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(boxDet, lv_color_hex(0xE0E0E0), 0);
    lv_obj_clear_flag(boxDet, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lblInfo = lv_label_create(boxDet);
    lv_label_set_text_fmt(lblInfo, "COD: %s\nEAN: %s\nMETA: %d", r.codigo,
                          r.barcode, r.quantidade);
    lv_obj_align(lblInfo, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_font(lblInfo, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_line_space(lblInfo, 4, 0);

    // Botoes: Lado a Lado no canto inferior direito
    // Botão PROXIMO (Seta) -> Canto Direito
    lv_obj_t *btnProx = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btnProx, 60, 60);
    lv_obj_align(btnProx, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_add_event_cb(btnProx, event_handler_proximo, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lP = lv_label_create(btnProx);
    lv_label_set_text(lP, ">");
    lv_obj_center(lP);

    // Botão SELECIONAR (OK) -> Esquerda da Seta
    lv_obj_t *btnEntrar = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btnEntrar, 120, 60);
    lv_obj_align(btnEntrar, LV_ALIGN_BOTTOM_RIGHT, -80,
                 -10); // Desloca 80px para esquerda
    lv_obj_set_style_bg_color(btnEntrar, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(btnEntrar, event_handler_entrar, LV_EVENT_CLICKED,
                        NULL);

    lv_obj_t *lblEntrar = lv_label_create(btnEntrar);
    lv_label_set_text(lblEntrar, "OK");
    lv_obj_center(lblEntrar);

  } else {
    // TELA VAZIA
    // IMAGE LOGO
    lv_obj_t *imgLogo = lv_img_create(lv_scr_act());
    lv_img_set_src(imgLogo, &AIPLAN_LOGO_FINAL_2020);
    lv_obj_align(imgLogo, LV_ALIGN_TOP_MID, 0, 60); // Ajuste vertical

    // Recolorir para VERMELHO (Design)
    // Como é ALPHA_1BIT, podemos usar style_img_recolor
    lv_obj_set_style_img_recolor_opa(imgLogo, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(imgLogo, lv_palette_main(LV_PALETTE_RED), 0);

    lv_obj_t *lblVazio = lv_label_create(lv_scr_act());
    lv_label_set_text(lblVazio, "Aguardando Lista...");
    lv_obj_align(lblVazio, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_text_color(lblVazio, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(lblVazio, &lv_font_montserrat_14, 0);
  }
}

// Handler para o botão ZERAR
void event_handler_btn_reset_click(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    produtoSelecionado = -10; // Código para RESET
  }
}

// --- TELA PRODUCAO UPDATE ---
void mostrarTelaProducao(Receita r) {
  lv_obj_clean(lv_scr_act());
  recriarDebugCursor();

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xF0F0F0), 0);

  // Cabeçalho
  lv_obj_t *header = lv_obj_create(lv_scr_act());
  lv_obj_set_size(header, 240, 70);
  lv_obj_set_style_bg_color(header, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  // ID (Badge Redondo ou texto pequeno Canto Esquerdo)
  lv_obj_t *bgId = lv_obj_create(header);
  lv_obj_set_size(bgId, 40, 30);
  lv_obj_align(bgId, LV_ALIGN_TOP_LEFT, -10, -5);
  lv_obj_set_style_bg_color(bgId, lv_color_white(), 0);
  lv_obj_set_style_radius(bgId, 4, 0);
  lv_obj_clear_flag(bgId, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lblCod = lv_label_create(bgId);
  lv_label_set_text_fmt(lblCod, "%d", r.id);
  lv_obj_center(lblCod);
  lv_obj_set_style_text_color(lblCod, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_text_font(lblCod, &lv_font_montserrat_14, 0);

  // Descrição do Produto
  lv_obj_t *lblDesc = lv_label_create(header);
  lv_label_set_long_mode(lblDesc, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(lblDesc, 130); // Limita largura
  lv_label_set_text(lblDesc, r.descricao);
  lv_obj_align(lblDesc, LV_ALIGN_LEFT_MID, 35, 0);
  lv_obj_set_style_text_color(lblDesc, lv_color_white(), 0);
  lv_obj_set_style_text_font(lblDesc, &lv_font_montserrat_14, 0);

  // Botão SAIR
  lv_obj_t *btnVoltar = lv_btn_create(header);
  lv_obj_set_size(btnVoltar, 60, 40);
  lv_obj_align(btnVoltar, LV_ALIGN_RIGHT_MID, -5, 0);
  lv_obj_set_style_bg_color(btnVoltar, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_add_event_cb(btnVoltar, event_handler_btn_voltar_click,
                      LV_EVENT_CLICKED, NULL);

  lv_obj_t *lblV = lv_label_create(btnVoltar);
  lv_label_set_text(lblV, "SAIR");
  lv_obj_center(lblV);

  // Barra de Progresso
  barProducao = lv_bar_create(lv_scr_act());
  lv_obj_set_size(barProducao, 200, 20);
  lv_obj_align(barProducao, LV_ALIGN_TOP_MID, 0, 80);
  lv_bar_set_range(barProducao, 0, r.quantidade);
  lv_bar_set_value(barProducao, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(barProducao, lv_palette_main(LV_PALETTE_RED),
                            LV_PART_INDICATOR);

  // Card Contador Gigante
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, 200, 110);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 15);
  lv_obj_set_style_bg_color(card, lv_color_white(), 0);

  lblContador = lv_label_create(card);
  lv_label_set_text(lblContador, "0");
  lv_obj_center(lblContador);
  lv_obj_set_style_text_font(lblContador, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(lblContador, lv_palette_main(LV_PALETTE_RED), 0);

  // Texto Meta
  lv_obj_t *lblMeta = lv_label_create(lv_scr_act());
  lv_label_set_text_fmt(lblMeta, "META: %d", r.quantidade);
  lv_obj_align(lblMeta, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_style_text_color(lblMeta, lv_palette_main(LV_PALETTE_GREY), 0);

  // Botão ZERAR (Novo) - Canto Inferior Esquerdo
  lv_obj_t *btnReset = lv_btn_create(lv_scr_act());
  lv_obj_set_size(btnReset, 80, 40);
  lv_obj_align(btnReset, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_set_style_bg_color(btnReset, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_add_event_cb(btnReset, event_handler_btn_reset_click, LV_EVENT_CLICKED,
                      NULL);

  lv_obj_t *lblReset = lv_label_create(btnReset);
  lv_label_set_text(lblReset, "ZERAR");
  lv_obj_center(lblReset);
}

void atualizarContador(int qtd, int meta) {
  if (lblContador) {
    lv_label_set_text_fmt(lblContador, "%d", qtd);
  }
  if (barProducao) {
    lv_bar_set_value(barProducao, qtd, LV_ANIM_ON);
  }
}

// --- TELA DE DATA (NUMERICO) ---
char dataBuffer[10] = "";

void event_handler_num_data(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    const char *txt = lv_btnmatrix_get_btn_text(
        lv_event_get_target(e),
        lv_btnmatrix_get_selected_btn(lv_event_get_target(e)));
    if (strcmp(txt, "DEL") == 0) {
      if (strlen(dataBuffer) > 0)
        dataBuffer[strlen(dataBuffer) - 1] = '\0';
    } else if (strcmp(txt, "OK") == 0) {
      if (strlen(dataBuffer) == 6) { // Exige 6 digitos DDMMAA
        char dd[3] = {dataBuffer[0], dataBuffer[1], 0};
        char mm[3] = {dataBuffer[2], dataBuffer[3], 0};
        int d = atoi(dd);
        int m = atoi(mm);

        bool diaValido = (d >= 1 && d <= 31);
        bool mesValido = (m >= 1 && m <= 12);

        if (diaValido && mesValido) {
          produtoSelecionado = -3; // Confirmou DATA
        } else {
          // Data Invalida: Reseta
          memset(dataBuffer, 0, sizeof(dataBuffer));
          lv_label_set_text(lblDataDisplay, "DATA INVALIDA");
          return; // Sai para não desenhar o texto formatado embaixo agora
        }
      }
    } else {
      if (strlen(dataBuffer) < 6) {
        strcat(dataBuffer, txt);
      }
    }

    // Formata o display: DD/MM/AA
    if (strlen(dataBuffer) == 0 && strcmp(txt, "OK") == 0) {
      // Se limpamos buffer por erro, nao faz nada, deixa "DATA INVALIDA"
      // aparecer
    } else {
      char fmt[16] = "";
      int len = strlen(dataBuffer);
      for (int i = 0; i < len; i++) {
        if (i == 2 || i == 4) {
          strcat(fmt, "/");
        }
        char tmp[2] = {dataBuffer[i], '\0'};
        strcat(fmt, tmp);
      }
      lv_label_set_text(lblDataDisplay, fmt);
    }
  }
}

void mostrarTelaData() {
  lv_obj_clean(lv_scr_act());
  recriarDebugCursor();

  memset(dataBuffer, 0, sizeof(dataBuffer));

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xF0F0F0), 0);

  lv_obj_t *header = lv_obj_create(lv_scr_act());
  lv_obj_set_size(header, 240, 70);
  lv_obj_set_style_bg_color(header, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lblTitulo = lv_label_create(header);
  lv_label_set_text(lblTitulo, "DATA PRODUCAO");
  lv_obj_align(lblTitulo, LV_ALIGN_TOP_LEFT, 5, 5);
  lv_obj_set_style_text_color(lblTitulo, lv_color_white(), 0);
  lv_obj_set_style_text_font(lblTitulo, &lv_font_montserrat_14, 0);

  lv_obj_t *lblHint = lv_label_create(header);
  lv_label_set_text(lblHint, "DD/MM/AA");
  lv_obj_align(lblHint, LV_ALIGN_BOTTOM_LEFT, 5, -5);
  lv_obj_set_style_text_color(lblHint, lv_color_white(), 0);
  lv_obj_set_style_text_font(lblHint, &lv_font_montserrat_14, 0);

  lv_obj_t *btnVoltar = lv_btn_create(header);
  lv_obj_set_size(btnVoltar, 70, 40);
  lv_obj_align(btnVoltar, LV_ALIGN_RIGHT_MID, -5, 0);
  lv_obj_set_style_bg_color(btnVoltar, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_add_event_cb(btnVoltar, event_handler_btn_voltar_click,
                      LV_EVENT_CLICKED, NULL);

  lv_obj_t *lblV = lv_label_create(btnVoltar);
  lv_label_set_text(lblV, "VOLTAR");
  lv_obj_center(lblV);

  lv_obj_t *rectDisplay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(rectDisplay, 220, 50);
  lv_obj_align(rectDisplay, LV_ALIGN_TOP_MID, 0, 80);
  lv_obj_clear_flag(rectDisplay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(rectDisplay, lv_color_white(), 0);

  lblDataDisplay = lv_label_create(rectDisplay);
  lv_label_set_text(lblDataDisplay, "__/__/__");
  lv_obj_center(lblDataDisplay);
  lv_obj_set_style_text_font(lblDataDisplay, &lv_font_montserrat_22, 0);

  static const char *btnm_map[] = {"1", "2", "3", "\n", "4",   "5", "6",  "\n",
                                   "7", "8", "9", "\n", "DEL", "0", "OK", ""};

  lv_obj_t *btnm = lv_btnmatrix_create(lv_scr_act());
  lv_btnmatrix_set_map(btnm, btnm_map);
  lv_obj_set_size(btnm, 220, 180);
  lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_add_event_cb(btnm, event_handler_num_data, LV_EVENT_CLICKED, NULL);
}

DataProducao getDadosData() {
  DataProducao d = {1, 1, 24};
  if (strlen(dataBuffer) == 6) {
    char dd[3] = {dataBuffer[0], dataBuffer[1], 0};
    char mm[3] = {dataBuffer[2], dataBuffer[3], 0};
    char aa[3] = {dataBuffer[4], dataBuffer[5], 0};
    d.dia = atoi(dd);
    d.mes = atoi(mm);
    d.ano = atoi(aa);
  }
  return d;
}

// --- TELA DE RE (NUMERICO) ---
void event_handler_num(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    const char *txt = lv_btnmatrix_get_btn_text(
        lv_event_get_target(e),
        lv_btnmatrix_get_selected_btn(lv_event_get_target(e)));
    if (strcmp(txt, "DEL") == 0) {
      if (strlen(reBuffer) > 0)
        reBuffer[strlen(reBuffer) - 1] = '\0';
    } else if (strcmp(txt, "OK") == 0) {
      produtoSelecionado = -4; // Confirmou RE
    } else {
      if (strlen(reBuffer) < 4) {
        strcat(reBuffer, txt);
      }
    }
    lv_label_set_text(lblCRE, reBuffer);
  }
}

void mostrarTelaRE() {
  lv_obj_clean(lv_scr_act());
  recriarDebugCursor();

  memset(reBuffer, 0, sizeof(reBuffer));

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xF0F0F0), 0);

  lv_obj_t *header = lv_obj_create(lv_scr_act());
  lv_obj_set_size(header, 240, 70);
  lv_obj_set_style_bg_color(header, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lblTitulo = lv_label_create(header);
  lv_label_set_text(lblTitulo, "INFORME O RE");
  lv_obj_align(lblTitulo, LV_ALIGN_TOP_LEFT, 5, 10);
  lv_obj_set_style_text_color(lblTitulo, lv_color_white(), 0);
  lv_obj_set_style_text_font(lblTitulo, &lv_font_montserrat_14, 0);

  lv_obj_t *btnVoltar = lv_btn_create(header);
  lv_obj_set_size(btnVoltar, 70, 40);
  lv_obj_align(btnVoltar, LV_ALIGN_TOP_RIGHT, -5, 5);
  lv_obj_set_style_bg_color(btnVoltar, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_add_event_cb(btnVoltar, event_handler_btn_voltar_click,
                      LV_EVENT_CLICKED, NULL);

  lv_obj_t *lblV = lv_label_create(btnVoltar);
  lv_label_set_text(lblV, "VOLTAR");
  lv_obj_center(lblV);

  lv_obj_t *rectDisplay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(rectDisplay, 220, 50);
  lv_obj_align(rectDisplay, LV_ALIGN_TOP_MID, 0, 80);
  lv_obj_clear_flag(rectDisplay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(rectDisplay, lv_color_white(), 0);

  lblCRE = lv_label_create(rectDisplay);
  lv_label_set_text(lblCRE, "");
  lv_obj_center(lblCRE);
  lv_obj_set_style_text_font(lblCRE, &lv_font_montserrat_22, 0);

  static const char *btnm_map[] = {"1", "2", "3", "\n", "4",   "5", "6",  "\n",
                                   "7", "8", "9", "\n", "DEL", "0", "OK", ""};

  lv_obj_t *btnm = lv_btnmatrix_create(lv_scr_act());
  lv_btnmatrix_set_map(btnm, btnm_map);
  lv_obj_set_size(btnm, 220, 180);
  lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_add_event_cb(btnm, event_handler_num, LV_EVENT_CLICKED, NULL);
}

int getDadosRE() { return atoi(reBuffer); }

void mostrarMensagemProducaoConcluida() {
  if (msgConclusao == NULL) {
    msgConclusao = lv_obj_create(lv_scr_act());
    lv_obj_set_size(msgConclusao, 200, 150);
    lv_obj_center(msgConclusao);
    lv_obj_set_style_bg_color(msgConclusao, lv_palette_main(LV_PALETTE_GREEN),
                              0);

    lv_obj_t *lbl = lv_label_create(msgConclusao);
    lv_label_set_text(lbl, "PRODUCAO\nCONCLUIDA!");
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
  }
  lv_obj_clear_flag(msgConclusao, LV_OBJ_FLAG_HIDDEN);
}

void esconderMensagemProducaoConcluida() {
  if (msgConclusao != NULL) {
    lv_obj_add_flag(msgConclusao, LV_OBJ_FLAG_HIDDEN);
  }
}

int verificarToque() {
  int ret = produtoSelecionado;
  if (ret != 0) {
    produtoSelecionado = 0; // Consome
  }
  return ret;
}

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[240 * 20];

void setupDisplay() {
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  touchSpi.begin(TOUCH_XPT_CLK, TOUCH_XPT_MISO, TOUCH_XPT_MOSI, TOUCH_XPT_CS);
  touch.begin(touchSpi);
  touch.setRotation(0);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, 240 * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 240;
  disp_drv.ver_res = 320;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);

  // Inicializa tela
  mostrarCarouselSlave({}, 0);
}

void loopDisplay() { lv_timer_handler(); }