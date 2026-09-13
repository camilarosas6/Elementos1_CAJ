#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

//Variables globales
#define BOTON      16                   // Botón
#define LED      15                     // LED externo
#define TRAZA    14                     // Traza de ISR
#define VENTANA_MS   20                 // Ventana de tiempo para el rebote del botón

//Callback de la alarma para apagar el LED
int64_t led_off(alarm_id_t id, void *user_data) {

    gpio_put(LED, false);                           // Apagamos el LED
    return 0;

}

//Callback de la alarma one-shot
int64_t debounce(alarm_id_t id, void *user_data) {

    //Comprobamos si el botón sigue presionado después de VENTANA_MS
    if (gpio_get(BOTON) == 0) {

        gpio_put(LED, true);                            // Encendemos el LED
        add_alarm_in_ms(100, led_off, NULL, true);      // Programamos la alarma para apagar el LED después de 100 ms
    }

    gpio_set_irq_enabled(BOTON, GPIO_IRQ_EDGE_FALL, true);      // Rehabilitamos la IRQ del botón
    return 0;

}

//ISR del botón
void boton_isr(uint gpio, uint32_t events){

    gpio_put(TRAZA, 1);                                         // Traza de ISR
    gpio_set_irq_enabled(BOTON, GPIO_IRQ_EDGE_FALL, false);     // Deshabilitamos la IRQ del botón
    add_alarm_in_ms(VENTANA_MS, debounce, NULL, true);          // Programamos la alarma one-shot
    gpio_put(TRAZA, 0);                                         // Traza de ISR

}

int main(void)
{
    stdio_init_all();

    //TRAZA
    gpio_init(TRAZA);
    gpio_set_dir(TRAZA, GPIO_OUT);
    gpio_put(TRAZA, false);

    //LED
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    gpio_put(LED, false);

    //BOTÓN
    gpio_init(BOTON);
    gpio_set_dir(BOTON, GPIO_IN);
    gpio_pull_up(BOTON);

    // Configuramos la interrupción del botón
    gpio_set_irq_enabled_with_callback(BOTON, GPIO_IRQ_EDGE_FALL,true, &boton_isr);

    while (true)
    {
                                    // Indicar que el bucle está intencionalmente vacío y que el programa
        tight_loop_contents();      // está esperando trabajo proveniente de interrupciones/callbacks

    }
}