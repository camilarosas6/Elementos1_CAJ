#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"


// Variables para configurar la UART
#define UART_ID     uart0
#define BAUD_RATE   115200

#define TX_PIN      0
#define RX_PIN      1


int main(void) {

    // Inicialización de la UART y del USB
    stdio_init_all();


   // Configuración de los pines para la UART
    gpio_set_function(TX_PIN,GPIO_FUNC_UART);           // Configura el pin TX como función UART
    gpio_set_function(RX_PIN,GPIO_FUNC_UART);           // Configura el pin RX como función UART
    uart_init(UART_ID,BAUD_RATE);                       // Inicializa la UART con la velocidad de baudios especificada
    uart_set_format(UART_ID,8,1,UART_PARITY_NONE);      // Configura la UART con 8 bits de datos, 1 bit de parada y sin paridad
    uart_set_fifo_enabled(UART_ID,true);                // Habilita el FIFO de la UART

    while (true) {

        // PRINCIPAL -> USB -> APOYO -> UART -> PC
        int ch = getchar_timeout_us(0);

        if (ch != PICO_ERROR_TIMEOUT) {
            uart_putc_raw(UART_ID,(char)ch);
        }

        // APOYO -> UART -> PC -> APOYO -> USB
        while (uart_is_readable(UART_ID)) {
            char c =uart_getc(UART_ID);
            putchar(c);
        }
    }
    return 0;
}