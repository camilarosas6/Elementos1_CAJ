#include <stdio.h>
#include "pico/stdlib.h"

#include "hardware/timer.h"     //Acceder al TIMER1
#include "hardware/ticks.h"     //Cambiar como avanza TIMER1
#include "hardware/clocks.h"    //Conocer la frecuencia del reloj de referencia

//Variables globales
#define SIG_EXTERNA 15
#define TRAZA 14

//
volatile uint64_t t_subida_prev = 0;        //Tiempo de la última subida
volatile uint64_t t_bajada = 0;             //Tiempo de la última bajada
volatile uint64_t T_ticks = 0;              //Periodo medido en ticks
volatile uint64_t alto_ticks = 0;           // Tiempo que la señal permanece en alto
volatile bool medida_lista = false;         // Indica que ya hay una medición válida

void sig_isr(uint gpio, uint32_t events){

    uint64_t ahora = timer_time_us_64(timer1_hw);       //Leer el valor actual del contador TIMER1 en ticks/ciclos
    gpio_put(TRAZA,1);                                  //Traza en alto

    //Si el evento es flanco de subida
    if (events & GPIO_IRQ_EDGE_RISE){
        if (t_subida_prev !=0){
            T_ticks = ahora - t_subida_prev;     //Ciclos entre subidas
            medida_lista = true;                 //Indicar que la medida está lista
        }
        t_subida_prev = ahora;              //Actualizar el tiempo de la última subida
    }

    //Si el evento es flanco de bajada
    if (events & GPIO_IRQ_EDGE_FALL) {
        t_bajada = ahora;
        if (t_subida_prev != 0) {
            alto_ticks = t_bajada - t_subida_prev;  //Ciclos que la señal permanece en alto
        }
    }

    gpio_put(TRAZA,0);                  //Traza en bajo
}

int main()
{
    stdio_init_all();

    // Configurar TIMER1 para contar con la nueva base
    tick_stop(TICK_TIMER1);
    tick_start(TICK_TIMER1, 1);

    // Obtener la frecuencia del reloj de referencia
    uint32_t f_tick = clock_get_hz(clk_ref);

    //Configuración de GPIO14
    gpio_init(TRAZA);
    gpio_set_dir(TRAZA, GPIO_OUT);
    gpio_put(TRAZA,0);                  //Queremos que la traza empiece en nivel bajo

    //Configuración de GPIO15
    gpio_init(SIG_EXTERNA);
    gpio_set_dir(SIG_EXTERNA, GPIO_IN);
    gpio_disable_pulls(SIG_EXTERNA);

    // Activa las interrupciones del GPIO 15 para flancos de subida y bajada, y cuando ocurra alguno ejecuta sig_isr
                                        //PIN / QUÉ EVENTOS / Habilitar IRQ con callback / Funcion que se ejecuta            
    gpio_set_irq_enabled_with_callback(SIG_EXTERNA, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,true, &sig_isr);

    while (true) {
        if (medida_lista) {

            // Copiar las mediciones a variables locales
            uint64_t T_local = T_ticks;
            uint64_t alto_local = alto_ticks;

            // Indicar que esta medición ya fue tomada
            medida_lista = false;

            if (T_local != 0) {

                // Calcular frecuencia 
                float frecuencia = (float)f_tick / (float)T_local;

                // Calcular duty cycle
                float duty = ((float)alto_local / (float)T_local) * 100.0f;

                // Mostrar resultados
                printf("T = %llu ciclos | f = %.2f Hz | alto = %llu ciclos | duty = %.2f %%\n",
                (unsigned long long)T_local,
                frecuencia,
                (unsigned long long)alto_local,
                duty);

                // Limitar la velocidad de impresión
                sleep_ms(200);
            }
        }
    }
}
