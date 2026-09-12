/*
Práctica 1
Integrantes: Camila Rodríguez, Ana Rojas y Jonathan Hernández
Ejercicio Extra
*/
// Directivas de preprocesamiento para incluir bibliotecas
#include <stdio.h>
#include "pico/stdlib.h"
// Definición como constante de tipo entero sin signo
static const uint LED_PIN = 0;
int main()
{
    // Preparación y habilitación el pin del LED integrado para su uso
    digital
        gpio_init(LED_PIN);
    // Configuración de la dirección del pin (true = output)
    gpio_set_dir(LED_PIN, true);
    // Inicialización de un ciclo infinito
    while (true)
    {
        // Función toggle que desplaza un 1 a la posición exacta del pin
        del LED
            gpio_xor_mask(1u << LED_PIN);
        // Función de retardo que detiene el procesador durante 500 ms
        sleep_ms(500);
    }
}