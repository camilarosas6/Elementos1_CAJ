#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/structs/timer.h"
#include "hardware/structs/sio.h"


// Definiciones de pines
#define SIG_PIN       15
#define TRAZA_PIN     14

#define BTN_PIN       16
#define LED_PIN       25

#define VENTANA_MS    20


// Variables compartidas entre la ISR y el bucle principal, por eso son volatile
static volatile uint32_t t_subida_prev = 0;
static volatile uint32_t t_bajada = 0;

static volatile uint32_t T_ticks = 0;
static volatile uint32_t talto_ticks = 0;

static volatile bool listo = false;


// ISR de la señal
static void sig_isr(uint gpio, uint32_t events)
{
    // 1) Marca de tiempo del flanco.
    uint32_t t = timer_hw->timerawl;

    // 2) Traza de la ISR.
    sio_hw->gpio_set = 1u << TRAZA_PIN;


    // Flanco de subida
    if (events & GPIO_IRQ_EDGE_RISE)
    {

        if (t_subida_prev != 0)
        {
            // Tiempo del ciclo que acaba de terminar.
            T_ticks = t - t_subida_prev;

            // Tiempo en alto del ciclo que acaba de terminar.
            talto_ticks = t_bajada - t_subida_prev;

            // Avisar de datos listos
            listo = true;
        }

        // Referencia de tiempo para el próximo ciclo
        t_subida_prev = t;
    }


    // Flanco de bajada
    else if (events & GPIO_IRQ_EDGE_FALL)
    {
        // Marca de tiempo de la bajada
        t_bajada = t;
    }


    // 3) Traza de la ISR.
    // baja la traza para que el osciloscopio pueda medir la duración de la ISR.
    sio_hw->gpio_clr = 1u << TRAZA_PIN;


    // Reconocimiento de la IRQ
    gpio_acknowledge_irq(gpio, events);
}


// Callback antirrebote de la alarma one-shot
static int64_t fin_debounce(alarm_id_t id, void *user_data)
{
    // Comprobamos si el nivel del botón sigue siendo bajo.
    if (gpio_get(BTN_PIN) == 0)
    {
        // Aceptar el evento
        gpio_xor_mask(1u << LED_PIN);
    }


    // Habilitar IRQ boton
    gpio_set_irq_enabled(
        BTN_PIN,
        GPIO_IRQ_EDGE_FALL,
        true
    );


    // alarma terminada, no se repite
    return 0;
}

// ISR Botón
static void btn_isr(uint gpio, uint32_t events)
{
    // Evitar eventos repetidos durante el tiempo de rebote
    gpio_set_irq_enabled(
        BTN_PIN,
        GPIO_IRQ_EDGE_FALL,
        false
    );


    // Programar la alarma one-shot para el tiempo de rebote
    add_alarm_in_ms(
        VENTANA_MS,
        fin_debounce,
        NULL,
        true
    );


    // IRQ
    gpio_acknowledge_irq(gpio, events);
}


// main
int main(void)
{
    // inicialización
    stdio_init_all();


    // Frecuenciometro
     * SIG_PIN = GPIO 15
    // Deshabilitamos los pull-ups y pull-downs internos.
    gpio_init(SIG_PIN);
    gpio_set_dir(SIG_PIN, GPIO_IN);
    gpio_disable_pulls(SIG_PIN);


    // Salida canal 2 osciloscopio
    gpio_init(TRAZA_PIN);
    gpio_set_dir(TRAZA_PIN, GPIO_OUT);
    gpio_clr(TRAZA_PIN);


    // callback ambos flancos
    gpio_set_irq_enabled_with_callback(
        SIG_PIN,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
        true,
        &sig_isr
    );


    // boton
    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);


    // detectar flanco bajada
    gpio_pull_up(BTN_PIN);


    gpio_set_irq_enabled(
        BTN_PIN,
        GPIO_IRQ_EDGE_FALL,
        true
    );


    // funcion irq se registra con gpio_set_irq_callback
    gpio_set_irq_callback(btn_isr);


    // habilitar interrupciones globales
    irq_set_enabled(IO_IRQ_BANK0, true);


    // bucle principal
    while (true)
    {
        // cálculo
        if (listo)
        {
            // copiar variables antes de que la ISR las modifique
            uint32_t T = T_ticks;
            uint32_t talto = talto_ticks;


            // limpiar bandera de datos listos
            listo = false;



            float f_hz = 1000000.0f / (float)T;


            // duty cycle en porcentaje
            float duty =
                100.0f * (float)talto / (float)T;


            // resultados
            printf(
                "T = %lu us | f = %.2f Hz | duty = %.1f %%\n",
                T,
                f_hz,
                duty
            );


            // medicion continua
            sleep_ms(200);
        }
    }


    return 0;
}
