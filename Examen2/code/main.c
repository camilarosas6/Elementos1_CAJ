
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "consola.h"
#include <stdint.h>
#include <stdbool.h>
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/rand.h"
#include <ctype.h>

// COMUNICACION UART
#define UART_ID     uart0
#define BAUD_RATE   115200
#define TX_PIN      0
#define RX_PIN      1

// POTENCIOMETRO
#define ADC_PIN_POTENCIOMETRO       26
#define ADC_CANAL_POTENCIOMETRO     0
#define ADC_PERIODO_US              10000u

// FILTRO EMA
#define FILTRO_ALPHA               0.125f

// LED
#define LED_PIN                    14
#define LED_PWM_TOP                999u
#define LED_PWM_FRECUENCIA_HZ      1000u

// BOTON
#define BOTON_PIN                  2
#define BOTON_ANTIRREBOTE_US       20000u

// SERVO
#define SERVO_PIN                   16
#define SERVO_PWM_TOP               19999u
#define SERVO_PWM_TICK_HZ           1000000u
#define SERVO_PULSO_CERRADO_US      1000u
#define SERVO_PULSO_ABIERTO_US      2000u

// MUESTRAS FILTRADAS Y SIN FILTRAR
#define ADC_MUESTRAS_POR_INTENTO 100u
#define INTENTOS_MAXIMOS        3u

typedef enum {
    DETENIDO,
    BUSCANDO,
    ABIERTA
} estados_juego;

// VARIABLES GLOBALES
static uint32_t potenciometro_lectura_codigo_adc = 0;
static uint32_t potenciometro_lectura_numerica = 0;
static uint64_t tiempo_ultima_lectura_us = 0;

static float potenciometro_lectura_filtrada = 0.0f;
static bool filtro_inicializado = false;

static int numero_objetivo = 0;
static uint32_t led_distancia = 0;
static uint32_t led_duty_porcentaje = 0;

static volatile uint64_t boton_tiempo_ultimo_flanco_us = 0;
static volatile bool boton_pulsacion_pendiente = false;
static bool servo_abierto = false;

static estados_juego estado = DETENIDO;
static int combinacion_secreta[3] = {0, 0, 0};
static uint32_t indice_numero_secreto = 0;
static uint32_t intentos_restantes = 3;

static void juego_iniciar(void);
static void juego_confirmar_numero(void);

static uint64_t tiempo_inicio_partida_us = 0;
static uint64_t tiempo_ultima_partida_us = 0;

static bool hay_resultado_ultima_partida = false;
static bool ultima_partida_ganada = false;

static uint16_t muestras_sin_filtro[3][100];
static uint16_t muestras_filtradas[3][100];
static uint16_t cantidad_muestras[3] = {0};
static int fila_a_enviar = -1;


// INICIALIZA EL POTENCIOMETRO (ADC)
static void potenciometro_init(void) {
    adc_init();
    adc_gpio_init(ADC_PIN_POTENCIOMETRO);
    adc_select_input(ADC_CANAL_POTENCIOMETRO);
}

// LECTURA DEL POTENCIOMETRO Y FILTRO EMA
static void tarea_adc(void) {
    uint64_t ahora_us = time_us_64();

    if (ahora_us - tiempo_ultima_lectura_us < ADC_PERIODO_US) {
        return;
    }

    tiempo_ultima_lectura_us = ahora_us;

    // Conservar la lectura original, sin filtro.
    potenciometro_lectura_codigo_adc = adc_read();

    if (!filtro_inicializado) {
        // Arrancar el filtro desde la primera lectura real.
        potenciometro_lectura_filtrada =(float)potenciometro_lectura_codigo_adc;

        filtro_inicializado = true;

    } else {
        // Actualizar el filtro EMA.
        potenciometro_lectura_filtrada += FILTRO_ALPHA * ((float)potenciometro_lectura_codigo_adc - potenciometro_lectura_filtrada);
    }

    // Redondear el resultado filtrado a un codigo ADC entero.
    uint32_t codigo_filtrado = (uint32_t)(potenciometro_lectura_filtrada + 0.5f);

    // Convertir el codigo filtrado al rango de 0 a 99.
    potenciometro_lectura_numerica = codigo_filtrado * 100u / 4096u;

    if (estado == BUSCANDO) {
        uint32_t intento = 3u - intentos_restantes;
        uint16_t posicion = cantidad_muestras[intento];

        if (posicion < ADC_MUESTRAS_POR_INTENTO) {
            muestras_sin_filtro[intento][posicion] =
                (uint16_t)potenciometro_lectura_codigo_adc;

            muestras_filtradas[intento][posicion] =
                (uint16_t)codigo_filtrado;

            cantidad_muestras[intento]++;
        }
    }

        if (fila_a_enviar >= 0) {
        uint32_t intento = (uint32_t)fila_a_enviar / ADC_MUESTRAS_POR_INTENTO;

        uint32_t muestra = (uint32_t)fila_a_enviar % ADC_MUESTRAS_POR_INTENTO;

        if (muestra < cantidad_muestras[intento]) {
            consola_printf(
                "\n%u,%u,%u,%u",
                (unsigned)(intento + 1u),
                (unsigned)(muestra + 1u),
                (unsigned)muestras_sin_filtro[intento][muestra],
                (unsigned)muestras_filtradas[intento][muestra]
            );
        }

        fila_a_enviar++;

        if (fila_a_enviar == (int)(INTENTOS_MAXIMOS * ADC_MUESTRAS_POR_INTENTO)) {

            fila_a_enviar = -1;
            consola_printf("\nFin de muestras.\n> ");
        }
    }
}

// INICIALIZA EL LED (PWM)
static void led_init(void) {
    gpio_set_function(LED_PIN, GPIO_FUNC_PWM);

    uint slice_led = pwm_gpio_to_slice_num(LED_PIN);

    pwm_config configuracion = pwm_get_default_config();

    pwm_config_set_wrap(&configuracion, LED_PWM_TOP);

    float divisor =(float)clock_get_hz(clk_sys) / ((float)LED_PWM_FRECUENCIA_HZ * (float)(LED_PWM_TOP + 1u));

    pwm_config_set_clkdiv(&configuracion, divisor);

    pwm_init(slice_led, &configuracion, true); // Iniciar el PWM con level 0 (apagado)
}

// CALCULA EL DUTY CYCLE DEL LED
static uint32_t calcular_duty_led(uint32_t distancia) {
    if (distancia >= 20u) {
        return 0u;
    }

    return 100u - 5u * distancia;
}

// ACTUALIZA EL LEVEL SEGUN LA DISTANCIA
static void tarea_led(void) {

    //Candado: Si el filtro no está inicializado o el estado no es BUSCANDO, no encender el LED.
    if (estado != BUSCANDO || !filtro_inicializado) {
        led_distancia = 0;
        led_duty_porcentaje = 0;

        pwm_set_gpio_level(LED_PIN, 0);
        return;
    }

    led_distancia = (uint32_t)abs((int)potenciometro_lectura_numerica - numero_objetivo);

    led_duty_porcentaje = calcular_duty_led(led_distancia);

    uint32_t nivel_pwm = led_duty_porcentaje * (LED_PWM_TOP + 1u) / 100u;

    pwm_set_gpio_level(LED_PIN, (uint16_t)nivel_pwm);
}

// INTERRUPCION DEL BOTON
static void boton_irq(uint gpio, uint32_t eventos) {
    if (gpio == BOTON_PIN && (eventos & GPIO_IRQ_EDGE_FALL) != 0u) {

        // Evitar nuevas IRQ del boton durante la verificacion.
        gpio_set_irq_enabled(BOTON_PIN,GPIO_IRQ_EDGE_FALL,false);

        boton_tiempo_ultimo_flanco_us = time_us_64();
        boton_pulsacion_pendiente = true;
    }
}

// INICIALIZA EL BOTON E INTERRUPCION
static void boton_init(void) {
    gpio_init(BOTON_PIN);
    gpio_set_dir(BOTON_PIN, GPIO_IN);
    gpio_pull_up(BOTON_PIN);

    gpio_set_irq_enabled_with_callback(BOTON_PIN, GPIO_IRQ_EDGE_FALL, true, boton_irq);
}

static void tarea_boton(void) {
    // Sin un evento nuevo, no comprobar ni confirmar el boton.
    if (!boton_pulsacion_pendiente) {
        return;
    }

    uint64_t ahora_us = time_us_64();

    // Esperar sin bloquear el programa.
    if (ahora_us - boton_tiempo_ultimo_flanco_us < BOTON_ANTIRREBOTE_US) {
        return;
    }

    // Comprobar una vez la entrada despues de los 20 ms.
    bool boton_presionado = (gpio_get(BOTON_PIN) == 0);

    // Este evento ya fue procesado, sea valido o no.
    boton_pulsacion_pendiente = false;

    // Permitir que una nueva pulsacion genere otra IRQ.
    gpio_set_irq_enabled(BOTON_PIN, GPIO_IRQ_EDGE_FALL, true);

    if (!boton_presionado) {
        return;
    }
    juego_confirmar_numero();
}

// POSICIONA EL SERVO
static void servo_posicionar(bool abrir) { // true: abrir, false: cerrar
    uint16_t ancho_pulso_us;

    if (abrir) {
        ancho_pulso_us = SERVO_PULSO_ABIERTO_US;
    } else {
        ancho_pulso_us = SERVO_PULSO_CERRADO_US;
    }

    pwm_set_gpio_level(SERVO_PIN, ancho_pulso_us);

    servo_abierto = abrir;
}

// INICIALIZA EL SERVO (PWM)
static void servo_init(void) {
    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);

    uint slice_servo = pwm_gpio_to_slice_num(SERVO_PIN);

    pwm_config configuracion = pwm_get_default_config();

    pwm_config_set_wrap(&configuracion, SERVO_PWM_TOP);

    float divisor = (float)clock_get_hz(clk_sys) / (float)SERVO_PWM_TICK_HZ;

    pwm_config_set_clkdiv(&configuracion, divisor);

    // Configurar sin arrancar el contador.
    pwm_init(slice_servo, &configuracion, false);

    // Preparar el pulso inicial de cerrado.
    servo_posicionar(false);

    // Arrancar con ese pulso ya establecido.
    pwm_set_enabled(slice_servo, true);
}

static void juego_iniciar(void) {
    // Descartar una pulsacion pendiente de la partida anterior.
    gpio_set_irq_enabled(BOTON_PIN,GPIO_IRQ_EDGE_FALL,false);

    boton_pulsacion_pendiente = false;

    servo_posicionar(false);

    indice_numero_secreto = 0;
    intentos_restantes = 3;

    cantidad_muestras[0] = 0;
    cantidad_muestras[1] = 0;
    cantidad_muestras[2] = 0;

    fila_a_enviar = -1;

    numero_objetivo = combinacion_secreta[indice_numero_secreto];

    tiempo_inicio_partida_us = time_us_64();

    estado = BUSCANDO;

    gpio_set_irq_enabled(BOTON_PIN,GPIO_IRQ_EDGE_FALL,true);
}

static void juego_confirmar_numero(void) {
    // Ignorar confirmaciones fuera de una partida activa.
    if (estado != BUSCANDO) {
        return;
    }

    int distancia = abs((int)potenciometro_lectura_numerica - numero_objetivo);

    // Acierto: tolerancia de una unidad.
    if (distancia <= 1) {
        indice_numero_secreto++;

        // Ya se confirmaron los tres numeros.
        if (indice_numero_secreto == 3u) {

            tiempo_ultima_partida_us = time_us_64() - tiempo_inicio_partida_us;
            ultima_partida_ganada = true;
            hay_resultado_ultima_partida = true;

            estado = ABIERTA;
            servo_posicionar(true);

            consola_printf("\nCombinacion correcta. Caja abierta.\n> ");
                consola_printf("Tiempo total: %.3f s\n",(double)tiempo_ultima_partida_us / 1000000.0);
            consola_printf("Numero de la perilla: %u\n> ",(unsigned)potenciometro_lectura_numerica);
            consola_printf("\n Combinacion %u %u %u\n> ",(unsigned)(combinacion_secreta[indice_numero_secreto-3u]),(unsigned)(combinacion_secreta[indice_numero_secreto-2u]),(unsigned)(combinacion_secreta[indice_numero_secreto-1u]));

            return;
        }

        // Pasar al siguiente numero de la combinacion.
        numero_objetivo = combinacion_secreta[indice_numero_secreto];
        consola_printf("\nAcerto el numero %u de 3.\n> ",(unsigned)(indice_numero_secreto));
        consola_printf("Numero de la perilla: %u\n> ",(unsigned)potenciometro_lectura_numerica);
        if (indice_numero_secreto == 1u) {
            consola_printf("\n Combinacion %u * *\n> ",(unsigned)(combinacion_secreta[indice_numero_secreto-1u]));
        }else if (indice_numero_secreto == 2u) {
            consola_printf("\n Combinacion %u %u *\n> ",(unsigned)(combinacion_secreta[indice_numero_secreto-2u]),(unsigned)(combinacion_secreta[indice_numero_secreto-1u]));
        }
        consola_printf("\nBuscando numero %u de 3.\n> ",(unsigned)(indice_numero_secreto + 1u));
        return;
    }

    // Error: perder un intento y volver al primer numero.
    intentos_restantes--;

    indice_numero_secreto = 0;
    numero_objetivo = combinacion_secreta[0];

    if (intentos_restantes == 0u) {
        tiempo_ultima_partida_us = time_us_64() - tiempo_inicio_partida_us;
        ultima_partida_ganada = false;
        hay_resultado_ultima_partida = true;

        estado = DETENIDO;
        servo_posicionar(false);

        consola_printf("\n>Sin intentos. Caja bloqueada.\n> ");

        return;
    }

    consola_printf("\nError, numero incorrecto. Quedan %u intentos.\n> ",(unsigned)intentos_restantes);
    consola_printf("Numero de la perilla: %u\n> ",(unsigned)potenciometro_lectura_numerica);
    consola_printf("\nBuscando nuevamente el primer numero de la combinacion.\n> ");
}

static bool leer_combinacion(
    const char *argumentos,
    int valores[3]
) {
    const char *cursor = argumentos;

    for (int i = 0; i < 3; i++) {
        char *fin;
        long valor = strtol(cursor, &fin, 10);

        bool valido =
            fin != cursor &&
            valor >= 0 &&
            valor <= 99 &&
            (*fin == '\0' || isspace((unsigned char)*fin));

        if (!valido) {
            return false;
        }

        valores[i] = (int)valor;
        cursor = fin;
    }

    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }

    return *cursor == '\0';
}

static void procesar(char *linea) {
    if (strcmp(linea, "nuevo") == 0) {
        for (int i = 0; i < 3; i++) {
            combinacion_secreta[i] = (int)(get_rand_32() % 100u);
        }

        juego_iniciar();
        consola_printf("ok\n");
        consola_printf("Ha iniciado la partida\n> ");
        consola_printf("Adivina el codigo secreto * * *\n");

    }  else if (
        strcmp(linea, "secreto") == 0 ||
        strncmp(linea, "secreto ", 8) == 0
    ) {
        int valores[3];

        if (!leer_combinacion(linea + 7, valores)) {
            consola_printf("error: usa secreto * * *, "
                "con valores de 0 a 99\n"
            );

            return;
        }

        for (int i = 0; i < 3; i++) {
            combinacion_secreta[i] = valores[i];
        }

        juego_iniciar();
        consola_printf("ok\n> ");
        for (int i = 0; i < 5; i++) {
            consola_printf("\n> ");
        }
        consola_printf("Codigo secreto establecido\n> ");
        consola_printf("Ha iniciado la partida\n> ");
        consola_printf("Adivina el codigo secreto * * *\n");
    }else if (strcmp(linea, "get stats") == 0) {
        switch (estado) {
            case DETENIDO:
                consola_printf("Estado: Caja bloqueada\n");
            break;

            case BUSCANDO:
                consola_printf("Estado: buscando numero %u de 3\n",(unsigned)(indice_numero_secreto + 1u));
                break;

            case ABIERTA:
                consola_printf("Estado: Caja abierta\n");
                break;
        }

        consola_printf("Intentos restantes: %u\n",(unsigned)intentos_restantes);

        consola_printf("ADC sin filtro: %u\n",(unsigned)potenciometro_lectura_codigo_adc);

        consola_printf("ADC filtrado: %u\n",(unsigned)(potenciometro_lectura_filtrada + 0.5f));

        consola_printf("Numero de la perilla: %u\n",(unsigned)potenciometro_lectura_numerica);

        consola_printf("Distancia: %u\n",(unsigned)led_distancia);

        consola_printf("Duty LED: %u %%\n",(unsigned)led_duty_porcentaje);   
        
        if (!hay_resultado_ultima_partida) {
            consola_printf("Ultima partida: Sin partidas finalizadas\n");
        } else if (ultima_partida_ganada) {
            consola_printf("Ultima partida: abierta, tiempo total %.3f s\n",(double)tiempo_ultima_partida_us / 1000000.0);
        } else {
            consola_printf("Ultima partida: bloqueada, tiempo total %.3f s\n",(double)tiempo_ultima_partida_us / 1000000.0);
        }

    }else if (strcmp(linea, "muestras") == 0) {
        if (fila_a_enviar >= 0) {
            consola_printf("error: consulta en curso\n");
            return;
        }

        consola_printf("Intento,Muestra,Sin_filtro,Filtrado\n");
        fila_a_enviar = 0;

    } else {
        consola_printf("error: comando desconocido\n");
    }
}

static void tarea_uart(void) {
    static char linea[64];
    if (consola_leer_linea(linea, sizeof linea)) {
        procesar(linea);
        consola_printf("> ");
    }
}

int main(void) {
    consola_init(UART_ID, TX_PIN, RX_PIN, BAUD_RATE);

    potenciometro_init();
    led_init();
    servo_init();
    boton_init();
    

    consola_printf("Caja Fuerte\n> ");

    while (true) {
        tarea_adc();
        tarea_led();
        tarea_boton();
        tarea_uart();
        consola_tarea();
    }
}
