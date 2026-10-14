#ifndef EXTRA_H
#define EXTRA_H

#include <stdbool.h>
#include <stdint.h>

#define EXTRA_FADE_MIN_MS 200u
#define EXTRA_FADE_MAX_MS 60000u

/* main.c configura el PWM y proporciona estas funciones para las salidas.
 * LED: level de 0 a 1000 (TOP = 999).
 * Buzzer: frecuencia en centesimas de Hz; 0 significa silencio.
 * Se ejecutan desde las alarmas: no deben imprimir ni esperar. */
typedef void (*extra_led_fn)(uint16_t level);
typedef void (*extra_buzzer_fn)(uint32_t frecuencia_cHz);

void extra_init(extra_led_fn escribir_led, extra_buzzer_fn escribir_buzzer);

/* Devuelven false si no se pudo crear la alarma o no se inicializo extra. */
bool melodia_iniciar(void);
bool tetris_iniciar(void);
void melodia_detener(void);

bool fade_iniciar(uint32_t duracion_ms);
/* Detener el fade conserva el ultimo brillo alcanzado. */
void fade_detener(void);

/* Avisos de fin: devuelven true una vez por finalizacion natural.
 * main.c los consulta para imprimir mediante consola_printf(). */
bool melodia_finalizada(void);
bool fade_finalizado(void);

#endif
