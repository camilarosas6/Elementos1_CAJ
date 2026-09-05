#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define TRAZA      14
#define BOTON      15
#define LED        16

#define VENTANA_MS 20

volatile bool evento_boton = false;


static void button_isr(uint gpio, uint32_t events){
  if (gpio == BOTON && (events & GPIO_IRQ_EDGE_FALL)){
    //Inicio de la ISR: Traza en alto
    gpio_put(TRAZA, 1);

    /* IMPORTANTE: Deshabilitamos inmediatamente nuevas IRQ provenientes del botón.
    * Así, los rebotes del botón NO pueden volver a ejecutar la ISR.*/
    gpio_set_irq_enabled(BOTON, GPIO_IRQ_EDGE_FALL, false);

    gpio_acknowledge_irq(BOTON, GPIO_IRQ_EDGE_FALL);
    evento_boton = true;

    //Fin de la ISR: Traza en bajo
    gpio_put(TRAZA, 0);
  }
}

//Estados del Debounce
typedef enum
{
    ESTABLE_ALTO,
    POSIBLE_BAJO,
    ESTABLE_BAJO,
    POSIBLE_ALTO

} estado_t;

int main(void)
{
  stdio_init_all();

  gpio_init(TRAZA);
  gpio_set_dir(TRAZA, GPIO_OUT);
  // Estado normal fuera de una IRQ 
  gpio_put(TRAZA, 0);

  gpio_init(LED);
  gpio_set_dir(LED, GPIO_OUT);
  gpio_put(LED, 0);

  gpio_init(BOTON);
  gpio_set_dir(BOTON, GPIO_IN);
  gpio_pull_up(BOTON);

  //Configuración de la IRQ
  gpio_set_irq_enabled_with_callback(BOTON, GPIO_IRQ_EDGE_FALL, true, &button_isr);

  //Estado del Debounce
  estado_t estado = ESTABLE_ALTO;
  absolute_time_t t_cambio;

  while (true){

    bool nivel_alto = gpio_get(BOTON);


    switch (estado){

      //Boton sin presionar
      case ESTABLE_ALTO:

        //Si hay posible pulsación
        if (evento_boton && !nivel_alto){
          evento_boton = false;
          estado = POSIBLE_BAJO;
          t_cambio = get_absolute_time();
        }
      break;

      //Posible pulsación
      case POSIBLE_BAJO:

        // Si regresó a HIGH antes de 20 ms, fue un rebote o una pulsación inválida.
        if (nivel_alto){
          estado = ESTABLE_ALTO;
          evento_boton = false;
          gpio_acknowledge_irq(BOTON, GPIO_IRQ_EDGE_FALL);
        }

        //Si permanece LOW durante 20 ms, la pulsación es válida.
        else if (absolute_time_diff_us(t_cambio, get_absolute_time()) >= (VENTANA_MS * 1000)){
          estado = ESTABLE_BAJO;
          //Cambiar estado del LED
          gpio_xor_mask(1u << LED);
        }
      break;

      //Botón pulsado
      case ESTABLE_BAJO:

        //Posible liberación del botón
        if (nivel_alto){
          estado = POSIBLE_ALTO;
          t_cambio = get_absolute_time();
        }
      break;

      //Posible liberación
      case POSIBLE_ALTO:

        //Si vuelve a LOW antes de 20 ms, todavía existe rebote.
        if (!nivel_alto){
          estado = ESTABLE_BAJO;
        }

        //Si permanece HIGH durante 20 ms, confirmamos la liberación.
        else if (absolute_time_diff_us(t_cambio, get_absolute_time()) >= (VENTANA_MS * 1000)){
          estado = ESTABLE_ALTO;
          evento_boton = false;
          gpio_acknowledge_irq(BOTON, GPIO_IRQ_EDGE_FALL);
          // Volver a permitir una nueva IRQ.
          gpio_set_irq_enabled(BOTON, GPIO_IRQ_EDGE_FALL, true);
        }
      break;
    }
    tight_loop_contents();
  }
}