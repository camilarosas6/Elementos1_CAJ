/*
Práctica 1
Integrantes: Camila Rodríguez, Ana Rojas y Jonathan Hernández
Código blink por toggle usando acceso directo a registros
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
        // Inversión del estado del pin mediante el registro toggle
        sio_hw->gpio_togl = bit;
        // Función de retardo que detiene el procesador durante 500 ms
        sleep_ms(500);
    }
}
