#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/regs/uart.h"

// Variables para configurar la UART
#define UART_ID         uart0
#define BAUD_RATE       115200

#define TX_PIN          0
#define RX_PIN          1

#define LED_PIN         16

// Trazas para osciloscopio
#define TRACE_ISR_PIN   14
#define TRACE_LOOP_PIN  13

// Retardo para el LED
#define RETARDO_LED_MS  0

// Variables para configurar el buffer de recepción
#define RX_BUF_SIZE 256

volatile uint8_t rx_buffer[RX_BUF_SIZE];
volatile uint16_t rx_head = 0;
volatile uint16_t rx_tail = 0;

// Variable para contar los caracteres descartados
volatile uint32_t descartados = 0;

// Error contadores
volatile uint32_t overruns = 0;
volatile uint32_t framing_errors = 0;

// Variables para configurar el buffer de transmisión
#define TX_BUF_SIZE 256

volatile uint8_t tx_buffer[TX_BUF_SIZE];
volatile uint16_t tx_head = 0;
volatile uint16_t tx_tail = 0;

// Tamaño máximo de la línea de comando (escritura en serial monitor)
#define LINE_SIZE 64

static char linea[LINE_SIZE];
static uint8_t n = 0;

// Configuración LED
uint32_t periodo_led_ms = 500;
uint32_t ultimo_led = 0;

bool estado_led = false;
bool led_habilitado = true;

// Configuración log
uint32_t ultimo_log = 0;

// Función para encolar un carácter en el buffer de transmisión
static void tx_encolar(uint8_t c) {

    uint16_t siguiente = (tx_head + 1) % TX_BUF_SIZE;

    // Si siguiente == tail, la cola esta llena
    if (siguiente != tx_tail) {

        tx_buffer[tx_head] = c;
        tx_head = siguiente;

        // Al habilitar TX, la ISR comenzara a vaciar la cola.
        uart_set_irq_enables(UART_ID,true,true);
    }
}

// Función para encolar un texto en el buffer de transmisión
static void tx_encolar_texto(const char *texto) {

    // Encolar cada carácter hasta el final del texto
    while (*texto != '\0') {
        tx_encolar((uint8_t)*texto);
        texto++;
    }
}

// ISR para la UART
static void on_uart_irq(void) {

    // Traza de ISR
    gpio_put(TRACE_ISR_PIN, 1);

    // Recepción de datos
    while (uart_is_readable(UART_ID)) {

        uint32_t dr = uart_get_hw(UART_ID)->dr;     // Leer el registro de datos

        // Revisar errores asociados al dato recibido
        if (dr & UART_UARTDR_FE_BITS) {
            framing_errors++;
        }

        // Revisar errores de sobrecarga
        if (dr & UART_UARTDR_OE_BITS) {
            overruns++;
        }

        // Extraer el dato recibido (8 bits)
        uint8_t c = (uint8_t)(dr & 0xFFu);

        // Siguiente lugar del buffer circular
        uint16_t siguiente = (rx_head + 1) % RX_BUF_SIZE;

        // Si siguiente == tail, la cola esta llena
        if (siguiente != rx_tail) {
            rx_buffer[rx_head] = c;
            rx_head = siguiente;
        }
        // Buffer circular lleno
        else {
            descartados++;
        }
    }

    // Transmisión de datos
    while (uart_is_writable(UART_ID) && tx_tail != tx_head) {
        uart_get_hw(UART_ID)->dr = tx_buffer[tx_tail];
        tx_tail = (tx_tail + 1) % TX_BUF_SIZE;
    }


    // Cola TX vacia
    if (tx_tail == tx_head) {
        // TX se apaga hasta que haya nuevos datos
        uart_set_irq_enables(UART_ID,true,false);
    }
    gpio_put(TRACE_ISR_PIN, 0);
}

// Función para mostrar estadísticas de errores
static void mostrar_stats(void) {

    uint32_t estado_irq = save_and_disable_interrupts();    // Deshabilitar interrupciones para leer variables compartidas
    uint32_t o = overruns;                                  // Guardar el valor de overruns
    uint32_t f = framing_errors;                            // Guardar el valor de framing_errors
    uint32_t d = descartados;                               // Guardar el valor de descartados

    restore_interrupts(estado_irq);         // Restaurar el estado de interrupciones

    char mensaje[128];      // Buffer para el mensaje de estadísticas

    snprintf(
        mensaje,
        sizeof(mensaje),
        "overruns=%lu framing_errors=%lu descartados=%lu\r\n",
        (unsigned long)o,
        (unsigned long)f,
        (unsigned long)d
    );

    tx_encolar_texto(mensaje);
}

// Función para procesar un comando recibido por UART
static void procesar_comando(char *cmd) {

    // Get stats
    if (strcmp(cmd, "get stats") == 0) {
        mostrar_stats();
    }

    // Set led on
    else if (strcmp(cmd, "set led on") == 0) {

        led_habilitado = true;
        estado_led = true;
        gpio_put(LED_PIN,estado_led);
        ultimo_led = time_us_32();
        tx_encolar_texto(" ok\r\n");

    }

    // Set led off
    else if (strcmp(cmd, "set led off") == 0) {

        led_habilitado = false;
        estado_led = false;
        gpio_put(LED_PIN,0);
        tx_encolar_texto(" ok\r\n");
    }

    //Set periodo 
    else {

        unsigned int nuevo_periodo;
        char extra;

        int encontrados = sscanf(
            cmd,
            "set periodo %u %c",
            &nuevo_periodo,
            &extra
        );


        if (encontrados == 1 && nuevo_periodo > 0) {
            periodo_led_ms = nuevo_periodo;
            tx_encolar_texto(" ok\r\n");
        } else {
            tx_encolar_texto(" error: comando desconocido\r\n");
        }
    }
}

// Función para procesar los datos recibidos por UART
static void tarea_uart(void) {

    // Vaciar el buffer de recepción y procesar los comandos recibidos
    while (rx_tail != rx_head) {

        uint8_t c = rx_buffer[rx_tail];
        rx_tail = (rx_tail + 1) % RX_BUF_SIZE;

        // ECO
        tx_encolar(c);

        // Final de línea
        if (c == '\r'||c == '\n') {
            linea[n] = '\0';
            // Línea completa, procesar el comando
            if (n > 0) {
                procesar_comando(linea);
            }
            n = 0;
        }
        // Guardar el carácter en la línea si no se ha alcanzado el tamaño máximo
        else {
            if (n < LINE_SIZE - 1) {
                linea[n] = (char)c;
                n++;
            }
        }
    }
}

//Tarea para controlar el LED
static void tarea_led(uint32_t ahora) {

    if (!led_habilitado) {
        return;
    }

    if (ahora - ultimo_led >= periodo_led_ms * 1000u) {
        ultimo_led = ahora;
        estado_led = !estado_led;
        gpio_put(LED_PIN,estado_led);


        // Retardo para el LED
        #if RETARDO_LED_MS > 0

            busy_wait_ms(RETARDO_LED_MS);
        #endif
    }
}

// Tarea para logs periódicos
static void tarea_log(uint32_t ahora) {

    if (ahora - ultimo_log >= 1000000u) {

        ultimo_log = ahora;
        char mensaje[128];

        snprintf(
            mensaje,
            sizeof(mensaje),
            "[%lu us] tarea_log led=%s periodo=%lu ms\r\n",
            (unsigned long)ahora,
            led_habilitado ? "on" : "off",
            (unsigned long)periodo_led_ms
        );

        // Enviar el mensaje de log por UART
        tx_encolar_texto(mensaje);
    }
}

int main(void) {

    // Inicializar como GPIO la salida del LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN,GPIO_OUT);
    gpio_put(LED_PIN,0);

    // Inicializar como GPIO las salidas de trazas
    gpio_init(TRACE_ISR_PIN);
    gpio_set_dir(TRACE_ISR_PIN, GPIO_OUT);
    gpio_put(TRACE_ISR_PIN, 0);


    gpio_init(TRACE_LOOP_PIN);
    gpio_set_dir(TRACE_LOOP_PIN, GPIO_OUT);
    gpio_put(TRACE_LOOP_PIN, 0);

    // Inicializar como UART los pines de transmisión y recepción
    gpio_set_function(TX_PIN,GPIO_FUNC_UART);
    gpio_set_function(RX_PIN,GPIO_FUNC_UART);

    // Inicializar la UART con la velocidad BAUD_RATE
    uint actual_baud = uart_init(UART_ID,BAUD_RATE);

    // Configurar formato de datos: 8 bits, 1 bit de stop, sin paridad
    uart_set_format(UART_ID,8,1,UART_PARITY_NONE);


    // FIFO habilitada
    uart_set_fifo_enabled(UART_ID,true);

    // Interrupciones de la UART
    irq_set_exclusive_handler(UART0_IRQ,on_uart_irq);
    irq_set_enabled(UART0_IRQ,true);
    uart_set_irq_enables(UART_ID,true,false);

    // Calcular el error de velocidad de transmisión
    int32_t diferencia = (int32_t)actual_baud - (int32_t)BAUD_RATE;

    // Calcular el error en partes por millón (ppm)
    int32_t error_ppm = (int32_t)(((int64_t)diferencia * 1000000LL)/BAUD_RATE);

    char inicio[128];
    snprintf(
        inicio,
        sizeof(inicio),
        "\r\nUART lista: nominal=%u real=%u error=%ld ppm\r\n",
        BAUD_RATE,
        actual_baud,
        (long)error_ppm
    );

    tx_encolar_texto(inicio);

    // Inicializar marcas de tiempo
    uint32_t ahora = time_us_32();

    ultimo_led = ahora;
    ultimo_log = ahora;

    // Bucle principal
    while (true) {

        gpio_put(TRACE_LOOP_PIN,1);

        ahora = time_us_32();

        // Cada vuelta
        tarea_uart();

        // Periodica
        tarea_led(ahora);

        // Cada segundo
        tarea_log(ahora);

        gpio_put(TRACE_LOOP_PIN,0);
    }
    return 0;
}