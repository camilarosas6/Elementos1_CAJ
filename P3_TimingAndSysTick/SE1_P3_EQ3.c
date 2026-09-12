#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/structs/sio.h"

#define LED_PIN     15
#define TRAZA_PIN   14

#define ALARM_NUM   0
#define ALARM_IRQ   TIMER0_IRQ_0

#define INTERVALO_US 250000u    // 250 ms

volatile uint32_t next_deadline;


// Rutina de atención a la interrupción
void on_alarm_irq(void)
{
    // 1. Limpiar la interrupción de la alarma
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // 2. Cambiar el estado del LED
    sio_hw->gpio_togl = 1u << LED_PIN;

    // 3. Generar la traza para el osciloscopio
    sio_hw->gpio_togl = 1u << TRAZA_PIN;

    // 4. Calcular el siguiente instante de interrupción
    next_deadline += INTERVALO_US;

    // 5. Programar nuevamente la alarma
    timer_hw->alarm[ALARM_NUM] = next_deadline;
}


int main(void)
{
    // ----------------------------
    // Configuración GPIO LED
    // ----------------------------

    gpio_init(LED_PIN);

    // Configurar GP15 como salida usando SIO
    sio_hw->gpio_oe_set = 1u << LED_PIN;

    // LED inicialmente apagado
    sio_hw->gpio_clr = 1u << LED_PIN;


    // ----------------------------
    // Configuración GPIO de traza
    // ----------------------------

    gpio_init(TRAZA_PIN);

    // Configurar GP14 como salida usando SIO
    sio_hw->gpio_oe_set = 1u << TRAZA_PIN;

    // Traza inicialmente en LOW
    sio_hw->gpio_clr = 1u << TRAZA_PIN;


    // ----------------------------
    // Configuración de la IRQ
    // ----------------------------

    // Registrar la función que atenderá la interrupción
    irq_set_exclusive_handler(ALARM_IRQ, on_alarm_irq);

    // Limpiar cualquier interrupción pendiente
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // Habilitar la alarma 0 dentro del TIMER
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);

    // Habilitar la IRQ en el NVIC
    irq_set_enabled(ALARM_IRQ, true);


    // ----------------------------
    // Programar primera alarma
    // ----------------------------

    // Leer el contador actual del temporizador
    uint32_t ahora = timer_hw->timerawl;

    // Primera interrupción dentro de 250 ms
    next_deadline = ahora + INTERVALO_US;

    // Programar alarma
    timer_hw->alarm[ALARM_NUM] = next_deadline;


    // ----------------------------
    // Programa principal
    // ----------------------------

    while (true)
    {
        // El CPU queda libre.
        // El temporizador funciona mediante interrupciones.
        tight_loop_contents();
    }
}