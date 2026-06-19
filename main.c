/*
 * ╔══════════════════════════════════════════════════════╗
 * ║         URNA ELETRÔNICA — ELE2IT / SENAI             ║
 * ║  Autor  : Lukeines12 (Lemos)                         ║
 * ║  MCU    : ESP32                                       ║
 * ║  Display: LCD 16x2 (modo paralelo 4 bits)            ║
 * ║  Input  : Teclado Matricial 4x4                      ║
 * ║  IoT    : Wegnology via HTTP POST (JSON)             ║
 * ╚══════════════════════════════════════════════════════╝
 *
 * Pinagem:
 *  LCD RS  → GPIO 19    LCD EN  → GPIO 18
 *  LCD D4  → GPIO 5     LCD D5  → GPIO 17
 *  LCD D6  → GPIO 16    LCD D7  → GPIO 4
 *
 *  Teclado linhas  → GPIO 13, 12, 14, 27
 *  Teclado colunas → GPIO 26, 25, 33, 32
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_client.h"

static const char *TAG = "URNA";

/* ─── Wi-Fi ─────────────────────────────────────────── */
#define WIFI_SSID      "Lukeines_nets"
#define WIFI_PASSWORD  "Internekeines#1010"

/* ─── Wegnology ──────────────────────────────────────── */
#define WEGNOLOGY_DEVICE_ID  "6a0f08d50016cc5247af557d"
#define WEGNOLOGY_ACCESS_KEY "0c05c13e-971b-4a65-833c-6e63fa07a172"
#define WEGNOLOGY_URL        "https://api.app.wnology.io/applications/" \
                             WEGNOLOGY_DEVICE_ID "/devices/" \
                             WEGNOLOGY_DEVICE_ID "/state"

/* ─── Pinagem LCD 16x2 (modo 4 bits) ────────────────── */
#define LCD_RS  GPIO_NUM_19
#define LCD_EN  GPIO_NUM_18
#define LCD_D4  GPIO_NUM_5
#define LCD_D5  GPIO_NUM_17
#define LCD_D6  GPIO_NUM_16
#define LCD_D7  GPIO_NUM_4

/* ─── Pinagem Teclado 4x4 ────────────────────────────── */
#define NUM_ROWS 4
#define NUM_COLS 4

static const gpio_num_t ROW_PINS[NUM_ROWS] = {
    GPIO_NUM_13, GPIO_NUM_12, GPIO_NUM_14, GPIO_NUM_27
};
static const gpio_num_t COL_PINS[NUM_COLS] = {
    GPIO_NUM_26, GPIO_NUM_25, GPIO_NUM_33, GPIO_NUM_32
};

/* Mapa do teclado: linha × coluna */
static const char KEYMAP[NUM_ROWS][NUM_COLS] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

/* ─── Candidatos ─────────────────────────────────────── */
#define MAX_CANDIDATOS 7
#define MAX_DIGITOS    2

typedef struct {
    char numero[3];   /* Ex: "13" */
    char nome[17];    /* Máx 16 chars + '\0' para LCD */
} Candidato;

static const Candidato candidatos[MAX_CANDIDATOS] = {
    {"13", "Lula"},
    {"22", "Flavio"},
    {"14", "Renan"},
    {"27", "Zema"},
    {"33", "Daciolo"},
    {"12", "Lemos"},
    {"00", "BRANCO"},
};

/* ─── Estado global da urna ──────────────────────────── */
#define MAX_TURNOS 2

static int  votos[MAX_CANDIDATOS] = {0};
static int  turno_atual           = 1;
static bool wifi_conectado        = false;
static bool voto_computado        = false;  /* flag para debounce */

/* Dígitos digitados pelo eleitor */
static char digitos[MAX_DIGITOS + 1] = "";
static int  num_digitos              = 0;

/* ══════════════════════════════════════════════════════
 *  LCD — Funções de baixo nível
 * ══════════════════════════════════════════════════════ */

static void lcd_pulse_en(void)
{
    gpio_set_level(LCD_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(LCD_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
}

static void lcd_send_nibble(uint8_t nibble)
{
    gpio_set_level(LCD_D4, (nibble >> 0) & 0x01);
    gpio_set_level(LCD_D5, (nibble >> 1) & 0x01);
    gpio_set_level(LCD_D6, (nibble >> 2) & 0x01);
    gpio_set_level(LCD_D7, (nibble >> 3) & 0x01);
    lcd_pulse_en();
}

static void lcd_send_byte(uint8_t byte, bool is_data)
{
    gpio_set_level(LCD_RS, is_data ? 1 : 0);
    lcd_send_nibble(byte >> 4);   /* nibble alto */
    lcd_send_nibble(byte & 0x0F); /* nibble baixo */
    vTaskDelay(pdMS_TO_TICKS(2));
}

static void lcd_command(uint8_t cmd)  { lcd_send_byte(cmd,  false); }
static void lcd_data(uint8_t data)    { lcd_send_byte(data, true);  }

static void lcd_init(void)
{
    /* Configura GPIOs */
    gpio_num_t pins[] = {LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7};
    for (int i = 0; i < 6; i++) {
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pins[i], 0);
    }

    vTaskDelay(pdMS_TO_TICKS(50)); /* aguarda display estabilizar */

    /* Sequência de inicialização em modo 4 bits (datasheet HD44780) */
    lcd_send_nibble(0x03); vTaskDelay(pdMS_TO_TICKS(5));
    lcd_send_nibble(0x03); vTaskDelay(pdMS_TO_TICKS(1));
    lcd_send_nibble(0x03); vTaskDelay(pdMS_TO_TICKS(1));
    lcd_send_nibble(0x02); /* entra em modo 4 bits */

    lcd_command(0x28); /* 2 linhas, fonte 5x8 */
    lcd_command(0x0C); /* display on, cursor off */
    lcd_command(0x06); /* incrementa cursor */
    lcd_command(0x01); /* limpa display */
    vTaskDelay(pdMS_TO_TICKS(2));
}

static void lcd_clear(void)
{
    lcd_command(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
}

static void lcd_set_cursor(uint8_t col, uint8_t row)
{
    uint8_t row_offset[] = {0x00, 0x40};
    lcd_command(0x80 | (col + row_offset[row]));
}

static void lcd_print(const char *str)
{
    while (*str) lcd_data((uint8_t)(*str++));
}

/* ══════════════════════════════════════════════════════
 *  Teclado matricial 4x4
 * ══════════════════════════════════════════════════════ */

static void teclado_init(void)
{
    for (int r = 0; r < NUM_ROWS; r++) {
        gpio_set_direction(ROW_PINS[r], GPIO_MODE_OUTPUT);
        gpio_set_level(ROW_PINS[r], 1);
    }
    for (int c = 0; c < NUM_COLS; c++) {
        gpio_set_direction(COL_PINS[c], GPIO_MODE_INPUT);
        gpio_pullup_en(COL_PINS[c]);
    }
}

/* Retorna o caractere pressionado ou '\0' se nenhum */
static char teclado_scan(void)
{
    for (int r = 0; r < NUM_ROWS; r++) {
        gpio_set_level(ROW_PINS[r], 0); /* ativa linha */

        for (int c = 0; c < NUM_COLS; c++) {
            if (gpio_get_level(COL_PINS[c]) == 0) {
                vTaskDelay(pdMS_TO_TICKS(20)); /* debounce */
                if (gpio_get_level(COL_PINS[c]) == 0) {
                    /* aguarda soltar */
                    while (gpio_get_level(COL_PINS[c]) == 0)
                        vTaskDelay(pdMS_TO_TICKS(5));
                    gpio_set_level(ROW_PINS[r], 1);
                    return KEYMAP[r][c];
                }
            }
        }
        gpio_set_level(ROW_PINS[r], 1); /* desativa linha */
    }
    return '\0';
}

/* ══════════════════════════════════════════════════════
 *  Lógica de candidatos
 * ══════════════════════════════════════════════════════ */

/* Retorna índice do candidato pelo número digitado, ou -1 */
static int buscar_candidato(const char *num)
{
    for (int i = 0; i < MAX_CANDIDATOS; i++) {
        if (strcmp(candidatos[i].numero, num) == 0) return i;
    }
    return -1;
}

/* ══════════════════════════════════════════════════════
 *  UI — Telas do LCD
 * ══════════════════════════════════════════════════════ */

static void tela_boas_vindas(void)
{
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print("  URNA ELE2IT   ");
    lcd_set_cursor(0, 1);
    lcd_print("  Turno ");
    char buf[4];
    sprintf(buf, "%d/%d  ", turno_atual, MAX_TURNOS);
    lcd_print(buf);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

static void tela_digitar(void)
{
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print("Digite o numero:");
    lcd_set_cursor(0, 1);

    /* mostra dígitos e underscores para os restantes */
    char linha[17] = "";
    strcat(linha, digitos);
    for (int i = num_digitos; i < MAX_DIGITOS; i++) strcat(linha, "_");
    lcd_print(linha);
}

static void tela_candidato(int idx)
{
    lcd_clear();
    lcd_set_cursor(0, 0);
    char linha[17];
    snprintf(linha, sizeof(linha), "Num: %s", candidatos[idx].numero);
    lcd_print(linha);
    lcd_set_cursor(0, 1);
    lcd_print(candidatos[idx].nome);
}

static void tela_nulo(void)
{
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print("VOTO NULO");
    lcd_set_cursor(0, 1);
    lcd_print("# Confirmar  * X");
}

static void tela_confirmacao(int idx)
{
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print("# Confirmar");
    lcd_set_cursor(0, 1);
    lcd_print("* Cancelar");
    (void)idx;
}

static void tela_voto_ok(void)
{
    lcd_clear();
    lcd_set_cursor(2, 0);
    lcd_print("VOTO COMPUTADO");
    lcd_set_cursor(3, 1);
    lcd_print("Obrigado! :)");
    vTaskDelay(pdMS_TO_TICKS(2500));
}

static void tela_resultado(void)
{
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print("== RESULTADO ==");
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* Exibe cada candidato por 2 segundos */
    for (int i = 0; i < MAX_CANDIDATOS; i++) {
        lcd_clear();
        lcd_set_cursor(0, 0);
        char linha[17];
        snprintf(linha, sizeof(linha), "%-8s: %d vot", candidatos[i].nome, votos[i]);
        lcd_print(linha);
        lcd_set_cursor(0, 1);
        /* Barra visual simples */
        int total = 0;
        for (int j = 0; j < MAX_CANDIDATOS; j++) total += votos[j];
        int barra = (total > 0) ? (votos[i] * 16 / total) : 0;
        for (int b = 0; b < barra; b++) lcd_data(0xFF); /* bloco cheio */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void tela_fim(void)
{
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print(" FIM DA ELEICAO ");
    lcd_set_cursor(0, 1);
    lcd_print("Dados enviados! ");
    vTaskDelay(pdMS_TO_TICKS(3000));
}

/* ══════════════════════════════════════════════════════
 *  Wi-Fi
 * ══════════════════════════════════════════════════════ */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_conectado = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_conectado = true;
        ESP_LOGI(TAG, "Wi-Fi conectado");
    }
}

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,  &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();
}

/* ══════════════════════════════════════════════════════
 *  Wegnology — envia estado via HTTP POST
 * ══════════════════════════════════════════════════════ */

static void enviar_wegnology(void)
{
    if (!wifi_conectado) {
        ESP_LOGW(TAG, "Wi-Fi desconectado, nao enviou dados");
        return;
    }

    /* Monta JSON com todos os votos */
    char json[512];
    char dados[400] = "";

    for (int i = 0; i < MAX_CANDIDATOS; i++) {
        char campo[32];
        snprintf(campo, sizeof(campo), "\"%s\":%d",
                 candidatos[i].nome, votos[i]);
        strcat(dados, campo);
        if (i < MAX_CANDIDATOS - 1) strcat(dados, ",");
    }

    snprintf(json, sizeof(json),
             "{\"data\":{%s,\"turno\":%d}}", dados, turno_atual);

    ESP_LOGI(TAG, "Enviando: %s", json);

    esp_http_client_config_t config = {
        .url            = WEGNOLOGY_URL,
        .method         = HTTP_METHOD_POST,
        .skip_cert_common_name_check = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type",    "application/json");
    esp_http_client_set_header(client, "losant-access-key", WEGNOLOGY_ACCESS_KEY);
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status: %d", status);
    } else {
        ESP_LOGE(TAG, "Erro HTTP: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

/* ══════════════════════════════════════════════════════
 *  Máquina de estados da urna
 * ══════════════════════════════════════════════════════ */

typedef enum {
    ESTADO_AGUARDANDO,   /* exibe boas-vindas e espera 1° dígito  */
    ESTADO_DIGITANDO,    /* eleitor está digitando o número        */
    ESTADO_CONFIRMAR,    /* exibe candidato/nulo e pede confirmação*/
    ESTADO_RESULTADO,    /* exibe resultado parcial/final          */
    ESTADO_FIM,          /* eleição encerrada                      */
} EstadoUrna;

static EstadoUrna estado = ESTADO_AGUARDANDO;
static int candidato_selecionado = -1;

static void resetar_digitos(void)
{
    memset(digitos, 0, sizeof(digitos));
    num_digitos          = 0;
    candidato_selecionado = -1;
}

static void processar_tecla(char tecla)
{
    switch (estado) {

    /* ── AGUARDANDO ─────────────────────────────────── */
    case ESTADO_AGUARDANDO:
        if (tecla >= '0' && tecla <= '9') {
            resetar_digitos();
            digitos[num_digitos++] = tecla;
            estado = ESTADO_DIGITANDO;
            tela_digitar();
        }
        break;

    /* ── DIGITANDO ──────────────────────────────────── */
    case ESTADO_DIGITANDO:
        if (tecla >= '0' && tecla <= '9' && num_digitos < MAX_DIGITOS) {
            digitos[num_digitos++] = tecla;
            tela_digitar();

            if (num_digitos == MAX_DIGITOS) {
                /* número completo — busca candidato */
                candidato_selecionado = buscar_candidato(digitos);
                if (candidato_selecionado >= 0) {
                    tela_candidato(candidato_selecionado);
                } else {
                    tela_nulo();
                }
                estado = ESTADO_CONFIRMAR;
            }
        } else if (tecla == '*') {
            /* cancela e volta ao início */
            resetar_digitos();
            estado = ESTADO_AGUARDANDO;
            tela_boas_vindas();
        } else if (tecla == 'C') {
            /* apaga último dígito */
            if (num_digitos > 0) {
                digitos[--num_digitos] = '\0';
                tela_digitar();
            }
        }
        break;

    /* ── CONFIRMAR ──────────────────────────────────── */
    case ESTADO_CONFIRMAR:
        if (tecla == '#') {
            /* CONFIRMA VOTO */
            if (candidato_selecionado >= 0) {
                votos[candidato_selecionado]++;
            }
            tela_voto_ok();
            resetar_digitos();

            /* Verifica se encerrou o turno (exemplo: 5 votos por turno) */
            int total_turno = 0;
            for (int i = 0; i < MAX_CANDIDATOS; i++) total_turno += votos[i];

            /* Para o Wokwi/teste, mude VOTOS_POR_TURNO conforme necessário */
            #define VOTOS_POR_TURNO 5

            if (total_turno > 0 && total_turno % VOTOS_POR_TURNO == 0) {
                estado = ESTADO_RESULTADO;
                tela_resultado();

                if (turno_atual < MAX_TURNOS) {
                    turno_atual++;
                    estado = ESTADO_AGUARDANDO;
                    tela_boas_vindas();
                } else {
                    /* Eleição encerrada — envia resultado */
                    enviar_wegnology();
                    estado = ESTADO_FIM;
                    tela_fim();
                }
            } else {
                estado = ESTADO_AGUARDANDO;
                tela_boas_vindas();
            }

        } else if (tecla == '*') {
            /* CANCELA — volta ao início */
            resetar_digitos();
            estado = ESTADO_AGUARDANDO;
            tela_boas_vindas();
        }
        break;

    /* ── RESULTADO / FIM ────────────────────────────── */
    case ESTADO_RESULTADO:
    case ESTADO_FIM:
        /* Nenhuma tecla aceita após encerrar */
        break;
    }
}

/* ══════════════════════════════════════════════════════
 *  app_main
 * ══════════════════════════════════════════════════════ */

void app_main(void)
{
    /* NVS para Wi-Fi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_LOGI(TAG, "Iniciando Urna Eletronica ELE2IT");

    lcd_init();
    teclado_init();
    wifi_init();

    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print("  URNA ELE2IT  ");
    lcd_set_cursor(0, 1);
    lcd_print(" Conectando...  ");

    /* Aguarda Wi-Fi por até 10s */
    for (int i = 0; i < 20 && !wifi_conectado; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    tela_boas_vindas();
    estado = ESTADO_AGUARDANDO;

    ESP_LOGI(TAG, "Urna pronta!");

    while (1) {
        char tecla = teclado_scan();
        if (tecla != '\0') {
            ESP_LOGI(TAG, "Tecla: %c", tecla);
            processar_tecla(tecla);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
