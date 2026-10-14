/*
 * consola.c — La cocina de la librería.
 * Lo que lleva 'static' es privado: solo existe dentro de este archivo.
 */
#include "consola.h"
#include <stdarg.h>
#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/regs/uart.h"

/* ================= Privado ================= */

static uart_inst_t *uart_id;    /* qué UART usa la consola; lo guarda consola_init */

// Buffer circular de caracteres recibidos
#define RX_BUFFER_SIZE 256

static volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile size_t rx_head = 0;
static volatile size_t rx_tail = 0;

// Cola de caracteres pendientes de transmitir
#define TX_BUFFER_SIZE 256

static char tx_buffer[TX_BUFFER_SIZE];
static size_t tx_head = 0;
static size_t tx_tail = 0;

// 
static volatile consola_stats_t stats = {0};

/* ISR de recepción 
 * while haya datos: leer el byte, guardarlo en la mesa redonda si hay lugar
 * y contar descartados si no. Guarda y se va. */
static void on_uart_rx(void)
{
    while (uart_is_readable(uart_id))
    {
        // Leer el caracter y consultar el estado de recepcion
        uint8_t caracter = (uint8_t)uart_getc(uart_id);
        uint32_t errores = uart_get_hw(uart_id)->rsr;

        // Contar desbordamientos del FIFO de la UART
        if (errores & UART_UARTRSR_OE_BITS)
        {
            stats.overruns++;
        }

        // Contar errores de trama
        if (errores & UART_UARTRSR_FE_BITS)
        {
            stats.framing++;
        }

        // Reconocer y limpiar los indicadores de error
        if (errores != 0)
        {
            uart_get_hw(uart_id)->rsr = 0;
        }

        // Guardar el caracter en nuestro buffer circular
        size_t siguiente = (rx_head + 1) % RX_BUFFER_SIZE;

        if (siguiente == rx_tail)
        {
            stats.descartados++;
        }
        else
        {
            rx_buffer[rx_head] = caracter;
            rx_head = siguiente;
        }
    }
}

/* Saca un byte de la mesa redonda.
 * Devuelve el byte, o -1 si está vacía (head == tail). */
static int rx_sacar(void)
{
    // Si el buffer esta vacio, no hay caracteres pendientes
    if (rx_head == rx_tail)
    {
        return -1;
    }

    // Obtener el siguiente caracter pendiente
    int caracter = rx_buffer[rx_tail];

    // Avanzar la posicion de lectura
    rx_tail = (rx_tail + 1) % RX_BUFFER_SIZE;

    return caracter;
}

/* Mete un carácter a la cola de TX.
 * Si la cola está llena, el carácter se descarta. */
static void tx_meter(char c)
{
    // Calcular la siguiente posicion de escritura
    size_t siguiente = (tx_head + 1) % TX_BUFFER_SIZE;

    // Si alcanzamos la posicion de lectura, la cola esta llena
    if (siguiente == tx_tail)
    {
        return;
    }

    // Guardar el caracter y avanzar la posicion de escritura
    tx_buffer[tx_head] = c;
    tx_head = siguiente;
}


/* ================= Público ================= */

/* EJEMPLO RESUELTO */
uint32_t consola_init(uart_inst_t *uart, uint tx_pin, uint rx_pin, uint32_t baud) {
    uart_id = uart;                                     /* 1. recordar qué UART se usa */
    uint32_t baud_real = uart_init(uart_id, baud);      /* 2. encender el UART         */
    gpio_set_function(tx_pin, GPIO_FUNC_UART);          /* 3. conectar los pines       */
    gpio_set_function(rx_pin, GPIO_FUNC_UART);
    uart_set_format(uart_id, 8, 1, UART_PARITY_NONE);   /* 4. formato 8N1              */

    // Seleccionar la interrupcion de la UART utilizada
    uint irq_uart = UART0_IRQ;

    if (uart_id == uart1)
    {
        irq_uart = UART1_IRQ;
    }

    // Registrar y habilitar la funcion de recepcion
    irq_set_exclusive_handler(irq_uart, on_uart_rx);
    irq_set_enabled(irq_uart, true);

    // Habilitar interrupciones de recepcion, no de transmision
    uart_set_irq_enables(uart_id, true, false);

    return baud_real;                                   /* 5. devolver el baud real    */
}

/* Diapositiva "Armar la línea y responderla", con dos cambios:
 * el eco va a la cola de TX (tx_meter) y, al recibir Enter,
 * termina la línea con '\0' y devuelve true en lugar de llamar a procesar. */
bool consola_leer_linea(char *linea, size_t max)
{
    static size_t posicion = 0;
    static bool omitir_lf = false;
    static bool linea_larga = false;

    if (linea == NULL || max < 2)
    {
        return false;
    }

    while (true)
    {
        int caracter = rx_sacar();

        // Sin caracteres pendientes: regresar sin esperar
        if (caracter == -1)
        {
            return false;
        }

        // Si Enter llego como CRLF, ignorar el LF despues del CR
        if (omitir_lf)
        {
            omitir_lf = false;

            if (caracter == '\n')
            {
                continue;
            }
        }

        // Enter: terminar la linea
        if (caracter == '\r' || caracter == '\n')
        {
            omitir_lf = (caracter == '\r');

            tx_meter('\r');
            tx_meter('\n');

            if (linea_larga)
            {
                posicion = 0;
                linea_larga = false;
                linea[0] = '\0';

                consola_printf("error: linea demasiado larga\n");
                return false;
            }

            linea[posicion] = '\0';
            posicion = 0;
            return true;
        }

        // Aceptar caracteres ASCII imprimibles
        if (caracter < 32 || caracter > 126)
        {
            continue;
        }

        // Una linea demasiado larga se descarta hasta Enter
        if (linea_larga)
        {
            continue;
        }

        // Reservar una posicion para el terminador del texto
        if (posicion >= max - 1)
        {
            linea_larga = true;
            continue;
        }

        // Guardar el caracter y devolverlo a la consola como eco
        linea[posicion] = (char)caracter;
        posicion++;

        tx_meter((char)caracter);
    }
}

/* Arma el texto con vsnprintf y mete cada carácter a la cola de TX.
 * Antes de cada '\n', mete un '\r'. */
void consola_printf(const char *fmt, ...)
{
    char tmp[128];

    // Preparar el texto con los argumentos recibidos
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, args);
    va_end(args);

    // Guardar cada caracter del texto en la cola TX
    for (size_t i = 0; tmp[i] != '\0'; i++)
    {
        // Antes del salto de linea, agregar retorno de carro
        if (tmp[i] == '\n')
        {
            tx_meter('\r');
        }

        tx_meter(tmp[i]);
    }
}

/* Mientras la cola de TX tenga datos y uart_is_writable(uart_id),
 * saca un carácter y lo envía con uart_putc_raw. Nunca espera. */
void consola_tarea(void)
{
    // Enviar mientras haya caracteres y la UART tenga espacio
    while (tx_tail != tx_head && uart_is_writable(uart_id))
    {
        // Entregar a la UART el siguiente caracter pendiente
        uart_putc_raw(uart_id, tx_buffer[tx_tail]);

        // Avanzar la posicion de lectura de la cola
        tx_tail = (tx_tail + 1) % TX_BUFFER_SIZE;
    }
}

/* Copia los tres contadores en s. */
void consola_stats(consola_stats_t *s)
{
    s->overruns    = stats.overruns;
    s->framing     = stats.framing;
    s->descartados = stats.descartados;
}
