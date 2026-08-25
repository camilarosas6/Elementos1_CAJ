/*
Práctica 1
Integrantes: Camila Rodríguez, Ana Rojas y Jonathan Hernández
Código blink directo usando SDK
*/
// Directivas de preprocesamiento para incluir bibliotecas
#include <stdio.h>
#include "pico/stdlib.h"
int main()
{
    // Preparación y habilitación el pin del LED integrado para su uso
    digital
        gpio_init(PICO_DEFAULT_LED_PIN);
    // Configuración de la dirección del pin (true = output)
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);

    // Inicialización de un ciclo infinito
    while (true)
    {
        // Configura el estado lógico del pin en alto (1 o 3.3V)
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        // Función de retardo que detiene el procesador durante 500 ms
        sleep_ms(500);
        // Configura el estado lógico del pin en bajo (0 o 0V)
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        sleep_ms(500);
    }
}

