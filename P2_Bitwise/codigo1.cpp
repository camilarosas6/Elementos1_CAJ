#include <stdio.h>
#include <pico/stdlib.h>

#define BOTON 16
#define LED 15
#define VENTANA_MS 20

typedef enum {
    ESTABLE_ALTO, POSIBLE_BAJO, ESTABLE_BAJO, POSIBLE_ALTO
} estado_t;

int main(void)
{
    gpio_init(BOTON);
    gpio_set_dir(BOTON, false);
    gpio_pull_up(BOTON);

    gpio_init(LED);
    gpio_set_dir(LED, true);

    estado_t estado = ESTABLE_ALTO;
    absolute_time_t t_cambio;

    while (true) {
        bool nivel_alto = gpio_get(BOTON);

        switch (estado) {
            case ESTABLE_ALTO:
                if (!nivel_alto) {
                    estado = POSIBLE_BAJO;
                    t_cambio = get_absolute_time();
                }
                break;

            case POSIBLE_BAJO:
                if (nivel_alto) {
                    estado = ESTABLE_ALTO;
                } else if (absolute_time_diff_us(t_cambio, get_absolute_time()) > VENTANA_MS * 1000) {
                    estado = ESTABLE_BAJO;
                    gpio_xor_mask(1u << LED);
                }
                break;

            case ESTABLE_BAJO:
                if (nivel_alto) {
                    estado = POSIBLE_ALTO;
                    t_cambio = get_absolute_time();
                }
                break;

            case POSIBLE_ALTO:
                if (!nivel_alto) {
                    estado = ESTABLE_BAJO;
                } else if (absolute_time_diff_us(t_cambio, get_absolute_time()) > VENTANA_MS * 1000) {
                    estado = ESTABLE_ALTO;
                    gpio_xor_mask(1u << LED);
                }
                break;
        }
    }
}