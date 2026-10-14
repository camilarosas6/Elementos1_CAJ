#include "pico/stdlib.h"
#include "consola.h"
#include "extra.h"
#include <string.h>
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include <stdlib.h>
 
// Configuración UART
#define UART_ID   uart0
#define BAUD_RATE 115200
#define TX_PIN    0
#define RX_PIN    1
 
// Configuración del LED
#define LED_PIN        2
#define LED_FRECUENCIA 1000
#define LED_TOP        999
 
// Configuración del buzzer
#define BUZZER_PIN 4
#define BUZZER_DIVISOR 10
#define BUZZER_FRECUENCIA_INICIAL 2000
 
// Configuración del motor
#define MOTOR_PIN 8
#define MOTOR_DIVISOR 1
#define MOTOR_FRECUENCIA 20000
 
// Duty actual del LED: inicia al 25 %
static uint led_duty = 25;
 
// Frecuencia actual del buzzer: inicia a 0 (apagado)
static double buzzer_frecuencia = 0.0;
static uint16_t top_buzzer = 0;
 
// Duty actual del motor: inicia al 0 %
static uint motor_duty = 0;
// TOP del motor calculado dinámicamente según reloj
static uint16_t motor_top = 0;

/* Salidas que extra.c utiliza desde sus alarmas. Sin UART ni esperas. */
static void led_extra_set_level(uint16_t level)
{
    uint slice = pwm_gpio_to_slice_num(LED_PIN);
    uint canal = pwm_gpio_to_channel(LED_PIN);
    pwm_set_chan_level(slice, canal, level);
    led_duty = (uint)((uint32_t)level * 100u / (LED_TOP + 1u));
}

static void buzzer_extra_set_frecuencia(uint32_t frecuencia_cHz)
{
    uint slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    uint canal = pwm_gpio_to_channel(BUZZER_PIN);
    if (frecuencia_cHz == 0u)
    {
        pwm_set_chan_level(slice, canal, 0);
        buzzer_frecuencia = 0.0;
        return;
    }

    /* Las canciones incluyen notas mas altas que las del comando tono.
     * Calculo con enteros y redondeo, sin punto flotante para el PWM. */
    uint64_t numerador = (uint64_t)clock_get_hz(clk_sys) * 100u;
    uint64_t denominador = (uint64_t)BUZZER_DIVISOR * frecuencia_cHz;
    uint32_t cuentas = (uint32_t)((numerador + denominador / 2u) / denominador);
    if (cuentas < 1u) cuentas = 1u;
    if (cuentas > 65536u) cuentas = 65536u;
    top_buzzer = (uint16_t)(cuentas - 1u);
    pwm_set_wrap(slice, top_buzzer);
    pwm_set_chan_level(slice, canal, (uint16_t)(cuentas / 2u));
    buzzer_frecuencia = (double)frecuencia_cHz / 100.0;
}
 
static void led_set_duty(uint duty)
{
    // El control manual del LED tiene prioridad sobre el fade.
    fade_detener();
    // Convertir el porcentaje en pasos del contador PWM
    uint16_t level = (uint16_t)(duty * (LED_TOP + 1u) / 100u);
 
    uint slice = pwm_gpio_to_slice_num(LED_PIN);
    uint canal = pwm_gpio_to_channel(LED_PIN);
 
    pwm_set_chan_level(slice, canal, level);
 
    // Recordar el valor aplicado
    led_duty = duty;
}
 
static void motor_set_duty(uint duty)
{
    // Convertir el porcentaje en pasos del contador PWM
    uint16_t level = (uint16_t)(duty * (motor_top + 1u) / 100u);
 
    uint slice = pwm_gpio_to_slice_num(MOTOR_PIN);
    uint canal = pwm_gpio_to_channel(MOTOR_PIN);
 
    pwm_set_chan_level(slice, canal, level);
 
    // Recordar el valor aplicado
    motor_duty = duty;
}
 
static bool buzzer_set_frecuencia(double frecuencia)
{
    const double tonos_conocidos[] = {
        261.63, 293.66, 329.63, 349.23,
        392.00, 440.00, 493.88, 523.25
    };
    const size_t num_tonos = sizeof(tonos_conocidos) / sizeof(tonos_conocidos[0]);
 
    uint slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    uint canal = pwm_gpio_to_channel(BUZZER_PIN);
 
    // El valor 0 significa apagar el buzzer
    if (frecuencia == 0.0)
    {
        melodia_detener();
        pwm_set_chan_level(slice, canal, 0);
        buzzer_frecuencia = 0.0;
        return true;
    }
 
    bool tono_valido = false;
    for (size_t i = 0; i < num_tonos; i++)
    {
        if (frecuencia >= tonos_conocidos[i] - 0.01 && frecuencia <= tonos_conocidos[i] + 0.01)
        {
            tono_valido = true;
            break;
        }
    }
 
    if (!tono_valido)
    {
        return false;
    }
 
    // Calcular cuántas cuentas necesita cada periodo
    uint32_t cuentas = (uint32_t)(clock_get_hz(clk_sys) / (BUZZER_DIVISOR * frecuencia));
 
    // El contador permite entre 1 y 65536 cuentas
    if (cuentas == 0 || cuentas > 65536u)
    {
        return false;
    }

    // Detener la cancion solo despues de validar el tono manual.
    melodia_detener();
 
    top_buzzer = (uint16_t)(cuentas - 1u);      // Calcular el valor TOP para el generador PWM
    uint16_t level = (uint16_t)(cuentas / 5u);  // Duty de 20 %: 1/5 del periodo
 
    // Configurar el generador PWM con el nuevo TOP y nivel de duty
    pwm_set_wrap(slice, top_buzzer);
    pwm_set_chan_level(slice, canal, level);
 
    buzzer_frecuencia = frecuencia;
 
    return true;
}
 
// Decidir que hacer con el comando recibido
static void procesar_comando(const char *linea)
{
    if (linea[0] == '\0')
    {
        return;
    }
 
    if (strcmp(linea, "get stats") == 0)
    {
        // Reservar una estructura para recibir los contadores
        consola_stats_t estadisticas;
 
        // Obtener los valores desde la libreria
        consola_stats(&estadisticas);
 
        // Mostrar los descartes de recepcion
        consola_printf(
            "Overruns: %lu\n"
            "Framing: %lu\n"
            "Descartados: %lu\n",
            (unsigned long)estadisticas.overruns,
            (unsigned long)estadisticas.framing,
            (unsigned long)estadisticas.descartados
        );
    }
    else if (strcmp(linea, "melodia") == 0)
    {
        if (melodia_iniciar()) consola_printf("ok: melodia iniciada\n");
        else consola_printf("error: no se pudo iniciar la melodia\n");
    }
    else if (strcmp(linea, "tetris") == 0)
    {
        if (tetris_iniciar()) consola_printf("ok: tetris iniciado\n");
        else consola_printf("error: no se pudo iniciar tetris\n");
    }
    else if (strcmp(linea, "stop") == 0)
    {
        melodia_detener();
        fade_detener();
        consola_printf("ok: melodia y fade detenidos\n");
    }
    else if (strncmp(linea, "fade ", 5) == 0 || strcmp(linea, "fade") == 0)
    {
        if (strcmp(linea, "fade") == 0)
        {
            consola_printf("error: escribe fade <200 a 60000 ms>\n");
            return;
        }
        const char *texto_ms = linea + 5;
        size_t longitud = strlen(texto_ms);
        if (longitud == 0 || longitud > 5)
        {
            consola_printf("error: escribe fade <200 a 60000 ms>\n");
            return;
        }
        for (size_t i = 0; i < longitud; i++)
        {
            if (texto_ms[i] < '0' || texto_ms[i] > '9')
            {
                consola_printf("error: la duracion debe contener solo digitos\n");
                return;
            }
        }
        uint32_t duracion_ms = (uint32_t)strtoul(texto_ms, NULL, 10);
        if (duracion_ms < EXTRA_FADE_MIN_MS || duracion_ms > EXTRA_FADE_MAX_MS)
        {
            consola_printf("error: fade fuera de rango (200 a 60000 ms)\n");
        }
        else if (fade_iniciar(duracion_ms))
        {
            consola_printf("ok: fade iniciado\n");
        }
        else
        {
            consola_printf("error: no se pudo iniciar el fade\n");
        }
    }
    else if (strncmp(linea, "led ", 4) == 0)
    {
        const char *texto_duty = linea + 4;
        size_t longitud = strlen(texto_duty);
 
        // Aceptar de uno a tres digitos
        if (longitud == 0 || longitud > 3)
        {
            consola_printf("error: escribe led <0 a 100>\n");
            return;
        }
        else
        {
            // Comprobar que todos los caracteres sean digitos
            for (size_t i = 0; i < longitud; i++)
            {
                if (texto_duty[i] < '0' || texto_duty[i] > '9')
                {
                    consola_printf("error: el duty debe contener solo digitos\n");
                    return;
                }
               
            }
            // Convertir el texto validado en un numero
            int duty = atoi(texto_duty);
 
            if (duty > 100)
            {
                consola_printf("error: duty fuera de rango (0 a 100)\n");
            }
            else
            {
                led_set_duty((uint)duty);
                consola_printf("ok\n");
            }
        }
    }
    else if (strncmp(linea, "motor ", 6) == 0)
    {
        const char *texto_duty = linea + 6;
        size_t longitud = strlen(texto_duty);
 
        // Aceptar de uno a tres digitos
        if (longitud == 0 || longitud > 3)
        {
            consola_printf("error: escribe motor <0 a 100>\n");
            return;
        }
        else
        {
            // Comprobar que todos los caracteres sean digitos
            for (size_t i = 0; i < longitud; i++)
            {
                if (texto_duty[i] < '0' || texto_duty[i] > '9')
                {
                    consola_printf("error: el duty debe contener solo digitos\n");
                    return;
                }
            }
            // Convertir el texto validado en un numero
            int duty = atoi(texto_duty);
 
            if (duty > 100)
            {
                consola_printf("error: duty fuera de rango (0 a 100)\n");
            }
            else
            {
                motor_set_duty((uint)duty);
                consola_printf("ok\n");
            }
        }
    }
    else if (strncmp(linea, "tono ", 5) == 0)
    {
        // Saltar "tono " para encontrar el numero
        const char *texto_frecuencia = linea + 5;
        size_t longitud = strlen(texto_frecuencia);
 
        // Aceptar valores como 440 o 440.00
        if (longitud == 0 || longitud > 7)
        {
            consola_printf("error: escribe tono seguido de una frecuencia valida\n");
            return;
        }
 
        // Comprobar que todos los caracteres sean dígitos o un punto decimal
        bool tiene_punto = false;
        for (size_t i = 0; i < longitud; i++)
        {
            if (texto_frecuencia[i] < '0' || texto_frecuencia[i] > '9')
            {
                if (texto_frecuencia[i] == '.' && !tiene_punto)
                {
                    tiene_punto = true;
                    continue;
                }
                consola_printf("error: la frecuencia debe contener solo digitos o un punto decimal\n");
                return;
            }
        }
 
        char *fin = NULL;
        double frecuencia = strtod(texto_frecuencia, &fin);
        if (fin == texto_frecuencia || *fin != '\0')
        {
            consola_printf("error: la frecuencia no es valida\n");
            return;
        }
 
        // Solicitar el cambio de frecuencia
        if (buzzer_set_frecuencia(frecuencia))
        {
            // Calcular la frecuencia real que se está generando con TOP entero
            double frecuencia_real = (double)clock_get_hz(clk_sys) / (BUZZER_DIVISOR * (top_buzzer + 1u));
            consola_printf("ok\n");
            if (frecuencia == 0.0)
            {
                consola_printf("buzzer apagado\n");
            }
            else if (frecuencia >= 261.63 - 0.01 && frecuencia <= 261.63 + 0.01)
            {
                consola_printf("Tono Do4, buzzer a %.2f Hz, TOP a %u, Frecuencia real: %.2f Hz\n", frecuencia, top_buzzer, frecuencia_real);
            }
            else if (frecuencia >= 293.66 - 0.01 && frecuencia <= 293.66 + 0.01)
            {
                consola_printf("Tono Re4, buzzer a %.2f Hz, TOP a %u, Frecuencia real: %.2f Hz\n", frecuencia, top_buzzer, frecuencia_real);
            }
            else if (frecuencia >= 329.63 - 0.01 && frecuencia <= 329.63 + 0.01)
            {
                consola_printf("Tono Mi4, buzzer a %.2f Hz, TOP a %u, Frecuencia real: %.2f Hz\n", frecuencia, top_buzzer, frecuencia_real);
            }
            else if (frecuencia >= 349.23 - 0.01 && frecuencia <= 349.23 + 0.01)
            {
                consola_printf("Tono Fa4, buzzer a %.2f Hz, TOP a %u, Frecuencia real: %.2f Hz\n", frecuencia, top_buzzer, frecuencia_real);
            }
            else if (frecuencia >= 392.00 - 0.01 && frecuencia <= 392.00 + 0.01)
            {
                consola_printf("Tono Sol4, buzzer a %.2f Hz, TOP a %u, Frecuencia real: %.2f Hz\n", frecuencia, top_buzzer, frecuencia_real);
            }
            else if (frecuencia >= 440.00 - 0.01 && frecuencia <= 440.00 + 0.01)
            {
                consola_printf("Tono La4, buzzer a %.2f Hz, TOP a %u, Frecuencia real: %.2f Hz\n", frecuencia, top_buzzer, frecuencia_real);
            }
            else if (frecuencia >= 493.88 - 0.01 && frecuencia <= 493.88 + 0.01)
            {
                consola_printf("Tono Si4, buzzer a %.2f Hz, TOP a %u, Frecuencia real: %.2f Hz\n", frecuencia, top_buzzer, frecuencia_real);
            }
            else if (frecuencia >= 523.25 - 0.01 && frecuencia <= 523.25 + 0.01)
            {
                consola_printf("Tono Do5, buzzer a %.2f Hz, TOP a %u, Frecuencia real: %.2f Hz\n", frecuencia, top_buzzer, frecuencia_real);
            }
            else
            {
                consola_printf("error: Frecuencia no corresponde a un tono conocido\n");
            }
        }
        else
        {
            consola_printf("error: frecuencia no permitida\n");
        }
    }
    else
    {
        consola_printf("error: comando desconocido\n");
    }
}
 
// Revisar si hay una linea completa disponible
static void tarea_uart(void)
{
    static char linea[64];
 
    if (consola_leer_linea(linea, sizeof linea))
    {
        procesar_comando(linea);
    }
}
 
static void led_pwm_init(void)
{
    // Conectar GP2 al generador PWM
    gpio_set_function(LED_PIN, GPIO_FUNC_PWM);
 
    // Obtener el generador y el canal asociados al pin
    uint slice = pwm_gpio_to_slice_num(LED_PIN);
    uint canal = pwm_gpio_to_channel(LED_PIN);
 
    // Calcular el divisor utilizando la frecuencia del reloj
    float divisor = (float)clock_get_hz(clk_sys) /
                    (LED_FRECUENCIA * (LED_TOP + 1));
 
    // Preparar la configuracion del PWM
    pwm_config configuracion = pwm_get_default_config();
 
    pwm_config_set_clkdiv(&configuracion, divisor);
    pwm_config_set_wrap(&configuracion, LED_TOP);
 
    // Aplicar la configuracion, inicialmente detenido
    pwm_init(slice, &configuracion, false);
 
    // 250 pasos encendido de los 1000 pasos del ciclo: 25 %
    pwm_set_chan_level(slice, canal, 250);
 
    // Comenzar a generar los pulsos
    pwm_set_enabled(slice, true);
}
 
static void buzzer_pwm_init(void)
{
    // Conectar el pin al periférico PWM
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
 
    // Identificar el contador y el canal que corresponden al pin
    uint slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    uint canal = pwm_gpio_to_channel(BUZZER_PIN);
 
    // Calcular TOP para la frecuencia inicial
    uint16_t top = (uint16_t)(clock_get_hz(clk_sys) / (BUZZER_DIVISOR * BUZZER_FRECUENCIA_INICIAL) - 1u);
    top_buzzer = top;
 
    // Preparar la configuración del PWM
    pwm_config configuracion = pwm_get_default_config();
 
    pwm_config_set_clkdiv(&configuracion, BUZZER_DIVISOR);
    pwm_config_set_wrap(&configuracion, top);
 
    // Configurar el contador sin arrancarlo todavía
    pwm_init(slice, &configuracion, false);
 
    // Duty de 0 %: salida apagada
    pwm_set_chan_level(slice, canal, 0);
 
    // Arrancar el contador con la salida apagada
    pwm_set_enabled(slice, true);
}
 
static void motor_pwm_init(void)
{
    // Conectar GP8 al generador PWM
    gpio_set_function(MOTOR_PIN, GPIO_FUNC_PWM);
 
    // Obtener el generador y el canal asociados al pin
    uint slice = pwm_gpio_to_slice_num(MOTOR_PIN);
    uint canal = pwm_gpio_to_channel(MOTOR_PIN);
 
    // Calcular TOP utilizando la frecuencia del reloj y el divisor fijo de 1
    motor_top = (uint16_t)((clock_get_hz(clk_sys) / (MOTOR_DIVISOR * MOTOR_FRECUENCIA)) - 1u);
 
    // Preparar la configuracion del PWM
    pwm_config configuracion = pwm_get_default_config();
 
    pwm_config_set_clkdiv(&configuracion, (float)MOTOR_DIVISOR);
    pwm_config_set_wrap(&configuracion, motor_top);
 
    // Aplicar la configuracion, inicialmente detenido
    pwm_init(slice, &configuracion, false);
 
    // Iniciar con duty 0 %
    pwm_set_chan_level(slice, canal, 0);
 
    // Comenzar a generar los pulsos
    pwm_set_enabled(slice, true);
}
 
int main(void)
{
    consola_init(UART_ID, TX_PIN, RX_PIN, BAUD_RATE);
    led_pwm_init();
    buzzer_pwm_init();
    motor_pwm_init();
    extra_init(led_extra_set_level, buzzer_extra_set_frecuencia);
    consola_printf("\nPractica 7\n");
    consola_printf("Comandos:\n"
                   " led <0-100>\n"
                   " motor <0-100>\n"
                   " tono <Hz>\n"
                   " fade <200-60000 ms>\n"
                   " stop\n"
                   " get stats\n"
                   " melodia\n"
                   " tetris\n");

 
    while (true)
    {
        tarea_uart();
        if (melodia_finalizada()) consola_printf("melodia terminada\n");
        if (fade_finalizado()) consola_printf("fade terminado\n");
        consola_tarea();
        tight_loop_contents();
    }
}
