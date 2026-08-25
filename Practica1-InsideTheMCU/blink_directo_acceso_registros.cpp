/*
Práctica 1
Integrantes: Camila Rodríguez, Ana Rojas y Jonathan Hernández
Código blink directo usando acceso directo a registros
*/
// Directivas de preprocesamiento para incluir bibliotecas
#include <stdio.h>
#include "pico/stdlib.h"
int main()
{
    // Creación de una máscara de bits desplazando un '1' a la posición del
    pin const uint32_t bit = 1u << PICO_DEFAULT_LED_PIN;
    // Preparación y habilitación el pin del LED integrado para su uso
    digital
        gpio_init(PICO_DEFAULT_LED_PIN);
    // Configuración del pin como salida escribiendo en el registro Output
    Enable(OE)
        sio_hw->gpio_oe_set = bit;
    // Inicialización de un ciclo infinito
    while (true)
    {
        // Configuración del pin en estado ALTO (1) en un solo ciclo de
        reloj
            sio_hw->gpio_set = bit;
        // Función de retardo que detiene el procesador durante 500 ms
        sleep_ms(500);
        // Configuración del pin en estado BAJO (0) en un solo ciclo de
        reloj
            sio_hw->gpio_clr = bit;
        sleep_ms(500);
    }
}