#ifndef CONSOLA_H
#define CONSOLA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hardware/uart.h"

typedef struct {
    uint32_t overruns;
    uint32_t framing;
    uint32_t descartados;
} consola_stats_t;

uint32_t consola_init(uart_inst_t *uart, uint tx_pin, uint rx_pin, uint32_t baud);
bool consola_leer_linea(char *linea, size_t max);
void consola_printf(const char *fmt, ...);
void consola_tarea(void);
void consola_flush(void);
void consola_stats(consola_stats_t *s);

#endif