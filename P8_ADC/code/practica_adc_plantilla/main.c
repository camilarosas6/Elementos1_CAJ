/*
 * Práctica de ADC · Plantilla
 * DADO: PWM de 1 kHz en GP2, PWM del LED en GP15, comando stream, scheduler.
 * TODO: comandos adc, fs, filtro, ps, dimmer; la función filtrar y la tarea del dimmer.
 */
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "consola.h"
#include "adc_stream.h"

#define UART_ID     uart0
#define BAUD_RATE   115200
#define TX_PIN      0
#define RX_PIN      1

#define PIN_PWM     2       /* señal de prueba: 1 kHz, 50 %            */
#define PIN_LED     15      /* LED del dimmer                          */
#define PIN_PS      23      /* modo de la fuente de alimentación       */
#define LED_TOP     999     /* LED a 1 kHz, 1000 niveles (como en PWM) */

typedef enum { FILTRO_NINGUNO, FILTRO_MEDIA, FILTRO_EMA } filtro_t;

static uint32_t fs_hz  = 1000;               /* para stream            */
static filtro_t filtro = FILTRO_NINGUNO;
static bool     dimmer_activo = false;
static uint16_t muestras[STREAM_MAX];

/* ================= DADO ================= */

/* PWM de prueba: 1 kHz, 50 %, en GP2 */
static void pwm_prueba_init(void) {
    gpio_set_function(PIN_PWM, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_PWM);
    pwm_set_clkdiv(slice, clock_get_hz(clk_sys) / 1000000.0f);   /* 1 tick = 1 us */
    pwm_set_wrap(slice, 999);                                     /* 1 kHz         */
    pwm_set_gpio_level(PIN_PWM, 500);                             /* 50 %          */
    pwm_set_enabled(slice, true);
}

/* PWM del LED: 1 kHz, level de 0 a 1000 */
static void led_init(void) {
    gpio_set_function(PIN_LED, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_LED);
    pwm_set_clkdiv(slice, clock_get_hz(clk_sys) / 1000000.0f);
    pwm_set_wrap(slice, LED_TOP);
    pwm_set_gpio_level(PIN_LED, 0);
    pwm_set_enabled(slice, true);
}

static void led_set_level(uint16_t level) {
    pwm_set_gpio_level(PIN_LED, level);
}

/* ================= TODO ================= */

/* Aplica el filtro elegido a las n muestras de buf, en el mismo arreglo.
 * FILTRO_MEDIA: media móvil de 16. FILTRO_EMA: alfa = 1/16. */
static void filtrar(uint16_t *buf, uint32_t n) {
    static uint16_t buffer[16];
    static uint32_t suma = 0;
    static uint32_t i    = 0;
    static float    promedio = 0.0f;

    if (n == 0 || filtro == FILTRO_NINGUNO) return;

    /* Si es una captura de stream (n > 1), pre-cargar con la primera muestra */
    if (n > 1) {
        suma = 0;
        for (uint32_t k = 0; k < 16; k++) {
            buffer[k] = buf[0];
            suma += buf[0];
        }
        i = 0;
        promedio = (float)buf[0];
    }

    if (filtro == FILTRO_MEDIA) {
        for (uint32_t k = 0; k < n; k++) {
            uint16_t lectura = buf[k];
            suma = suma - buffer[i] + lectura;   /* sale la vieja, entra la nueva */
            buffer[i] = lectura;
            i = (i + 1) % 16;
            buf[k] = (uint16_t)(suma / 16);
        }
    } else if (filtro == FILTRO_EMA) {
        for (uint32_t k = 0; k < n; k++) {
            uint16_t lectura = buf[k];
            promedio = promedio + (1.0f / 16.0f) * ((float)lectura - promedio);
            buf[k] = (uint16_t)(promedio + 0.5f);
        }
    }
}

/* Cada 10 ms: si el dimmer está activo, leer ADC0, filtrar y ajustar el LED.
 * Además, contar cuántas veces cambia el level y reportarlo cada segundo. */
static void tarea_dimmer(void) {
    static uint32_t t_10ms       = 0;
    static uint32_t t_1s         = 0;
    static uint16_t ultimo_level = 0;
    static uint32_t cambios      = 0;

    if (!dimmer_activo) return;

    uint32_t ahora = time_us_32();
    if (t_10ms == 0) {
        t_10ms = ahora;
        t_1s   = ahora;
    }

    /* Cada 10 ms (10000 us): leer ADC0, filtrar y ajustar el LED */
    if (ahora - t_10ms >= 10000) {
        t_10ms += 10000;

        adc_select_input(0);
        uint16_t muestra = adc_read();
        filtrar(&muestra, 1);

        uint16_t level = (uint16_t)((uint32_t)muestra * 1000 / 4096);
        if (level != ultimo_level) {
            ultimo_level = level;
            cambios++;
        }
        led_set_level(level);
    }

    /* Cada 1 segundo (1000000 us): reportar cuántas veces cambió el level */
    if (ahora - t_1s >= 1000000) {
        t_1s += 1000000;
        consola_printf("level = %u, cambios = %lu\n> ",
                       ultimo_level, (unsigned long)cambios);
        cambios = 0;
    }
}

static void procesar(char *linea) {
    if (strncmp(linea, "stream ", 7) == 0) {                  /* DADO */
        char *fin;
        uint32_t canal = strtoul(&linea[7], &fin, 10);
        uint32_t n     = strtoul(fin, NULL, 10);
        if (!adc_stream_capturar(canal, n, fs_hz, muestras)) {
            consola_printf("error: canal 0-2, n hasta %u, Fs 100-10000, n/Fs hasta 5 s\n", STREAM_MAX);
            return;
        }
        filtrar(muestras, n);
        adc_stream_enviar(muestras, n);
    }
    /* TODO: adc <canal>, fs <Hz>, filtro <ninguno|media|ema>, ps <on|off>, dimmer <on|off> */
    else if (strncmp(linea, "adc ", 4) == 0) {
        char *fin;
        uint32_t canal = strtoul(&linea[4], &fin, 10);
        if (fin == &linea[4] || *fin != '\0' || canal > 2) {
            consola_printf("error: canal 0-2\n");
            return;
        }
        adc_select_input(canal);
        uint32_t suma = 0;
        for (int i = 0; i < 64; i++) {
            suma += adc_read();
        }
        uint16_t codigo = (uint16_t)(suma / 64);
        float volts = codigo * 3.3f / 4096.0f;
        consola_printf("codigo = %u, V = %.3f V\n", codigo, volts);
    }
    else if (strncmp(linea, "fs ", 3) == 0) {
        char *fin;
        uint32_t hz = strtoul(&linea[3], &fin, 10);
        if (fin == &linea[3] || *fin != '\0' || hz < 100 || hz > 10000) {
            consola_printf("error: Fs 100-10000\n");
            return;
        }
        fs_hz = hz;
        consola_printf("ok\n");
    }
    else if (strncmp(linea, "filtro ", 7) == 0) {
        if (strcmp(&linea[7], "ninguno") == 0) {
            filtro = FILTRO_NINGUNO;
            consola_printf("ok\n");
        } else if (strcmp(&linea[7], "media") == 0) {
            filtro = FILTRO_MEDIA;
            consola_printf("ok\n");
        } else if (strcmp(&linea[7], "ema") == 0) {
            filtro = FILTRO_EMA;
            consola_printf("ok\n");
        } else {
            consola_printf("error: filtro ninguno|media|ema\n");
        }
    }
    else if (strncmp(linea, "ps ", 3) == 0) {
        if (strcmp(&linea[3], "on") == 0) {
            gpio_put(PIN_PS, 1);
            consola_printf("ok\n");
        } else if (strcmp(&linea[3], "off") == 0) {
            gpio_put(PIN_PS, 0);
            consola_printf("ok\n");
        } else {
            consola_printf("error: ps on|off\n");
        }
    }
    else if (strncmp(linea, "dimmer ", 7) == 0) {
        if (strcmp(&linea[7], "on") == 0) {
            dimmer_activo = true;
            consola_printf("ok\n");
        } else if (strcmp(&linea[7], "off") == 0) {
            dimmer_activo = false;
            led_set_level(0);
            consola_printf("ok\n");
        } else {
            consola_printf("error: dimmer on|off\n");
        }
    }
    else if (strcmp(linea, "get stats") == 0) {
        consola_stats_t s;
        consola_stats(&s);
        consola_printf("overruns=%lu framing=%lu descartados=%lu\n",
                       (unsigned long)s.overruns,
                       (unsigned long)s.framing,
                       (unsigned long)s.descartados);
    }
    else {
        consola_printf("error: comando desconocido\n");
    }
}

/* ================= DADO ================= */

static void tarea_uart(void) {
    static char linea[64];
    if (consola_leer_linea(linea, sizeof linea)) {
        procesar(linea);
        consola_printf("> ");
    }
}

int main(void) {
    consola_init(UART_ID, TX_PIN, RX_PIN, BAUD_RATE);

    adc_init();
    adc_gpio_init(26);                    /* ADC0: potenciómetro    */
    adc_gpio_init(27);                    /* ADC1: PWM directo      */
    adc_gpio_init(28);                    /* ADC2: PWM con filtro RC */

    gpio_init(PIN_PS);
    gpio_set_dir(PIN_PS, GPIO_OUT);
    gpio_put(PIN_PS, 0);

    pwm_prueba_init();
    led_init();
    led_set_level(0);

    consola_printf("Practica ADC lista\n> ");

    while (true) {
        tarea_uart();
        tarea_dimmer();
        consola_tarea();
    }
}