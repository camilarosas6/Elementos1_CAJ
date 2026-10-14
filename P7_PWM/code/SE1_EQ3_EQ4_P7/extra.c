/* extra.c: solo las melodias y el fade; sin UART ni otro main.
 * El PWM se configura en main.c. Las alarmas cambian las salidas
 * mediante las funciones recibidas en extra_init(). */
#include "extra.h"
#include <stddef.h>
#include "pico/time.h"
#include "hardware/sync.h"

#define GAP_US         30000    /* 30 ms                                     */
 
/* Notas en centésimas de Hz: todo con enteros dentro de la ISR
* (el RP2040 no tiene FPU; el RP2350 sí, pero así funciona en ambos).     */
#define SIL            0u
#define DO4        26163u
#define RE4        29366u
#define MI4        32963u
#define FA4        34923u
#define SOL4       39200u
#define LA4        44000u
#define SI4        49388u
#define DO5        52325u
#define RE5        58733u
#define MI5        65926u
#define FA5        69846u
#define SOL5       78399u
#define LA5        88000u
 
/* Tempo */
#define BPM            120u
#define NEGRA          (60000u / BPM)        /* 500 ms                      */
#define CORCHEA        (NEGRA / 2u)          /* 250 ms                      */
#define NEGRA_P        (NEGRA * 3u / 2u)     /* 750 ms (negra con puntillo) */
#define BLANCA         (NEGRA * 2u)          /* 1000 ms                     */
 
/* Tempo propio para Tetris (más rápido) */
#define T_BPM          144u
#define T_NEGRA        (60000u / T_BPM)      /* 416 ms                      */
#define T_CORCHEA      (T_NEGRA / 2u)        /* 208 ms                      */
#define T_NEGRA_P      (T_NEGRA * 3u / 2u)   /* 624 ms (negra con puntillo) */
 
typedef struct {
    uint32_t f_cHz;    /* frecuencia en centésimas de Hz (0 = silencio) */
    uint16_t dur_ms;   /* duración de la nota                          */
} nota_t;
 
/* ----------------------------------------------------------------------------
*  MELODÍA: Himno a la alegría (Beethoven, dominio público)
*  30 notas, 4 duraciones distintas (corchea, negra, negra con puntillo, blanca)
* --------------------------------------------------------------------------*/
static const nota_t MELODIA[] = {
    /* Frase 1 */
    {MI4, NEGRA}, {MI4, NEGRA}, {FA4, NEGRA}, {SOL4, NEGRA},
    {SOL4, NEGRA}, {FA4, NEGRA}, {MI4, NEGRA}, {RE4, NEGRA},
    {DO4, NEGRA}, {DO4, NEGRA}, {RE4, NEGRA}, {MI4, NEGRA},
    {MI4, NEGRA_P}, {RE4, CORCHEA}, {RE4, BLANCA},
    /* Frase 2 */
    {MI4, NEGRA}, {MI4, NEGRA}, {FA4, NEGRA}, {SOL4, NEGRA},
    {SOL4, NEGRA}, {FA4, NEGRA}, {MI4, NEGRA}, {RE4, NEGRA},
    {DO4, NEGRA}, {DO4, NEGRA}, {RE4, NEGRA}, {MI4, NEGRA},
    {RE4, NEGRA_P}, {DO4, CORCHEA}, {DO4, BLANCA},
};
#define N_NOTAS (sizeof(MELODIA) / sizeof(MELODIA[0]))
 
/* ----------------------------------------------------------------------------
*  MELODÍA: Korobeiniki (canción folclórica rusa, s. XIX, dominio público)
*  Es el tema conocido de Tetris. Mi menor, 4/4, 144 BPM.
*  37 notas + 2 silencios, 3 duraciones (corchea, negra, negra con puntillo)
* --------------------------------------------------------------------------*/
static const nota_t TETRIS[] = {
    /* Compás 1-4: primera frase */
    {MI5, T_NEGRA}, {SI4, T_CORCHEA}, {DO5, T_CORCHEA},
    {RE5, T_NEGRA}, {DO5, T_CORCHEA}, {SI4, T_CORCHEA},
 
    {LA4, T_NEGRA}, {LA4, T_CORCHEA}, {DO5, T_CORCHEA},
    {MI5, T_NEGRA}, {RE5, T_CORCHEA}, {DO5, T_CORCHEA},
 
    {SI4, T_NEGRA_P}, {DO5, T_CORCHEA},
    {RE5, T_NEGRA}, {MI5, T_NEGRA},
 
    {DO5, T_NEGRA}, {LA4, T_NEGRA}, {LA4, T_NEGRA}, {SIL, T_NEGRA},
 
    /* Compás 5-8: segunda frase */
    {RE5, T_NEGRA_P}, {FA5, T_CORCHEA},
    {LA5, T_NEGRA}, {SOL5, T_CORCHEA}, {FA5, T_CORCHEA},
 
    {MI5, T_NEGRA_P}, {DO5, T_CORCHEA},
    {MI5, T_NEGRA}, {RE5, T_CORCHEA}, {DO5, T_CORCHEA},
 
    {SI4, T_NEGRA}, {SI4, T_CORCHEA}, {DO5, T_CORCHEA},
    {RE5, T_NEGRA}, {MI5, T_NEGRA},
 
    {DO5, T_NEGRA}, {LA4, T_NEGRA}, {LA4, T_NEGRA}, {SIL, T_NEGRA},
};
#define N_TETRIS (sizeof(TETRIS) / sizeof(TETRIS[0]))
 
static const uint16_t GAMMA[101] = {
       0,    0,    0,    0,    1,    1,    2,    3,    4,    5,
       6,    8,    9,   11,   13,   15,   18,   20,   23,   26,
      29,   32,   36,   39,   43,   47,   52,   56,   61,   66,
      71,   76,   82,   87,   93,   99,  106,  112,  119,  126,
     133,  141,  148,  156,  164,  173,  181,  190,  199,  208,
     218,  227,  237,  247,  258,  268,  279,  290,  302,  313,
     325,  337,  349,  362,  375,  388,  401,  414,  428,  442,
     456,  471,  485,  500,  516,  531,  547,  563,  579,  595,
     612,  629,  646,  664,  681,  699,  718,  736,  755,  774,
     793,  813,  832,  852,  873,  893,  914,  935,  957,  978,
    1000,
};
 
#define FADE_PASOS 200u

static extra_led_fn escribir_led = NULL;
static extra_buzzer_fn escribir_buzzer = NULL;

static volatile bool mel_activa = false;
static volatile bool mel_fin = false;
static bool mel_gap = false;
static size_t mel_idx = 0;
static size_t mel_n = 0;
static const nota_t *mel_tabla = NULL;
static alarm_id_t mel_alarm = 0;

static volatile bool fade_activo = false;
static volatile bool fade_fin = false;
static uint32_t fade_paso = 0;
static int64_t fade_paso_us = 0;
static alarm_id_t fade_alarm = 0;

/* Retorno negativo: siguiente instante respecto a la alarma anterior,
 * para no acumular el tiempo de ejecucion de cada callback. */
static int64_t melodia_cb(alarm_id_t id, void *user_data)
{
    (void)id;
    (void)user_data;
    if (!mel_activa) return 0;

    if (mel_gap)
    {
        mel_gap = false;
        escribir_buzzer(0);
        return -GAP_US;
    }

    if (mel_idx >= mel_n)
    {
        escribir_buzzer(0);
        mel_activa = false;
        mel_fin = true;
        mel_alarm = 0;
        return 0;
    }

    const nota_t *nota = &mel_tabla[mel_idx++];
    escribir_buzzer(nota->f_cHz);
    int64_t duracion_us = (int64_t)nota->dur_ms * 1000;

    /* Separar dos notas iguales consecutivas con un silencio de 30 ms. */
    if (GAP_US > 0 && duracion_us >= 2 * GAP_US && mel_idx < mel_n &&
        nota->f_cHz != 0u && mel_tabla[mel_idx].f_cHz == nota->f_cHz)
    {
        mel_gap = true;
        return -(duracion_us - GAP_US);
    }
    return -duracion_us;
}

/* Solo llamar con las interrupciones deshabilitadas en el core de main. */
static void melodia_cancelar(void)
{
    mel_activa = false;
    if (mel_alarm > 0) cancel_alarm(mel_alarm);
    mel_alarm = 0;
    mel_gap = false;
    mel_fin = false;
    if (escribir_buzzer != NULL) escribir_buzzer(0);
}

void melodia_detener(void)
{
    uint32_t irq = save_and_disable_interrupts();
    melodia_cancelar();
    restore_interrupts(irq);
}

static bool iniciar_cancion(const nota_t *tabla, size_t cantidad)
{
    uint32_t irq = save_and_disable_interrupts();
    melodia_cancelar();
    if (escribir_buzzer == NULL)
    {
        restore_interrupts(irq);
        return false;
    }

    mel_tabla = tabla;
    mel_n = cantidad;
    mel_idx = 0;
    mel_activa = true;
    /* false evita ejecutar el callback dentro de add_alarm_in_us(). */
    mel_alarm = add_alarm_in_us(1000, melodia_cb, NULL, false);
    bool ok = mel_alarm > 0;
    if (!ok) melodia_cancelar();
    restore_interrupts(irq);
    return ok;
}

bool melodia_iniciar(void)
{
    return iniciar_cancion(MELODIA, N_NOTAS);
}

bool tetris_iniciar(void)
{
    return iniciar_cancion(TETRIS, N_TETRIS);
}

static int64_t fade_cb(alarm_id_t id, void *user_data)
{
    (void)id;
    (void)user_data;
    if (!fade_activo) return 0;

    /* El paso 0 se aplica al iniciar: 1..100 sube y 101..200 baja. */
    uint32_t paso = ++fade_paso;
    uint8_t brillo = (uint8_t)(paso <= 100u ? paso : FADE_PASOS - paso);
    escribir_led(GAMMA[brillo]);

    if (paso >= FADE_PASOS)
    {
        fade_activo = false;
        fade_fin = true;
        fade_alarm = 0;
        return 0;
    }
    return -fade_paso_us;
}

static void fade_cancelar(void)
{
    fade_activo = false;
    if (fade_alarm > 0) cancel_alarm(fade_alarm);
    fade_alarm = 0;
    fade_fin = false;
}

void fade_detener(void)
{
    uint32_t irq = save_and_disable_interrupts();
    fade_cancelar();
    restore_interrupts(irq);
}

bool fade_iniciar(uint32_t duracion_ms)
{
    if (duracion_ms < EXTRA_FADE_MIN_MS || duracion_ms > EXTRA_FADE_MAX_MS)
        return false;

    uint32_t irq = save_and_disable_interrupts();
    if (escribir_led == NULL)
    {
        restore_interrupts(irq);
        return false;
    }
    fade_cancelar();
    fade_paso = 0;
    fade_paso_us = (int64_t)duracion_ms * 1000 / FADE_PASOS;
    fade_activo = true;
    fade_alarm = add_alarm_in_us((uint64_t)fade_paso_us, fade_cb, NULL, false);
    bool ok = fade_alarm > 0;
    if (ok) escribir_led(GAMMA[0]);
    else fade_cancelar();
    restore_interrupts(irq);
    return ok;
}

void extra_init(extra_led_fn led, extra_buzzer_fn buzzer)
{
    uint32_t irq = save_and_disable_interrupts();
    melodia_cancelar();
    fade_cancelar();
    escribir_led = led;
    escribir_buzzer = buzzer;
    restore_interrupts(irq);
}

bool melodia_finalizada(void)
{
    uint32_t irq = save_and_disable_interrupts();
    bool terminado = mel_fin;
    mel_fin = false;
    restore_interrupts(irq);
    return terminado;
}

bool fade_finalizado(void)
{
    uint32_t irq = save_and_disable_interrupts();
    bool terminado = fade_fin;
    fade_fin = false;
    restore_interrupts(irq);
    return terminado;
}
