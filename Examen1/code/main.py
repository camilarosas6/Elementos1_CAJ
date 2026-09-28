#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/regs/uart.h"

// Configuración UART
#define UART_ID         uart0
#define BAUD_RATE       115200
#define TX_PIN          0
#define RX_PIN          1

// LEDs y Botones
#define LED_ROJO        16
#define LED_VERDE       17
#define BOTON_J1        18
#define BOTON_J2        19

// Configuración del juego
#define PERIODO_INICIAL_MS 500          // Periodo inicial del parpadeo del LED rojo
#define ESPERA_MIN_MS      2000         // Tiempo aleatorio mínimo de espera antes del LED verde
#define ESPERA_MAX_MS      5000         // Tiempo aleatorio mínimo de espera antes del LED verde
#define VENTANA_VERDE_MS   5000         // Tiempo máximo para reaccionar después del LED verde
#define DEBOUNCE_US        20000ULL     // Antirrebote por software: 20 ms

// Buffer circular RX
#define RX_BUF_SIZE 256
volatile uint8_t rx_buffer[RX_BUF_SIZE];
volatile uint16_t rx_head = 0;
volatile uint16_t rx_tail = 0;
volatile uint32_t descartados = 0;      // Bytes descartados si el buffer circular de software se llena

// Errores UART
volatile uint32_t overruns = 0;
volatile uint32_t framing_errors = 0;

// Buffer circular TX
#define TX_BUF_SIZE 256
volatile uint8_t tx_buffer[TX_BUF_SIZE];
volatile uint16_t tx_head = 0;
volatile uint16_t tx_tail = 0;

// Comandos
#define LINE_SIZE 64
static char linea[LINE_SIZE];
static uint8_t n = 0;

// Estados del juego
typedef enum {
    DETENIDO,
    PREPARADOS,
    YA
} estado_juego_t;
volatile estado_juego_t estado_juego = DETENIDO;

// Última partida
typedef enum {
    SIN_PARTIDAS,
    GANA,
    FALSO,
    SIN_GANADOR
} estado_resultado_t;
volatile estado_resultado_t resultado = SIN_PARTIDAS;

// Datos de la partida
volatile uint32_t numero_ronda = 0;
volatile uint8_t ganador = 0;               //0=ninguno, 1=j1, 2=j2
volatile uint8_t jugador_falso = 0;         // Jugador que cometió salida en falso
volatile uint64_t t_inicio_verde = 0;       // Instante exacto en que se enciende el LED verde
volatile uint64_t tiempo_reaccion_us = 0;   // Tiempo de reacción del ganador
volatile bool resultado_pendiente = false;  // Indica que hay un resultado nuevo que mostrar por UART

// Puntaje
volatile uint32_t score_j1 = 0;
volatile uint32_t score_j2 = 0;


// Timer del juego
struct repeating_timer timer_juego;
volatile bool timer_activo = false;                                 // Indica si actualmente existe un timer activo
volatile uint32_t periodo_led_ms = PERIODO_INICIAL_MS;              // Periodo configurado por el usuario
volatile uint32_t periodo_timer_actual_ms = PERIODO_INICIAL_MS;     // Periodo que utiliza realmente la partida actual.
volatile uint32_t contador_timer = 0;                               // Número de ejecuciones del callback
volatile uint32_t cambios_objetivo = 0;                             // Número aleatorio de ejecuciones antes de encender verde
volatile bool rojo_encendido = false;                               // Estado actual del LED rojo


// Antirrebote - Timestamp de la última pulsación aceptada de cada jugador
volatile uint64_t ultimo_j1_us = 0;
volatile uint64_t ultimo_j2_us = 0;


// Prototipos
static void tx_encolar(uint8_t c);
static void tx_encolar_texto(const char *texto);
static void on_uart_irq(void);
static bool callback_juego(struct repeating_timer *t);
static bool iniciar_partida(void);
static void botones_isr(uint gpio,uint32_t events);
static void mostrar_stats(void);
static void procesar_comando(char *cmd);
static void tarea_uart(void);
static void tarea_resultado(void);


// Encolar caracteres para Transmisión
static void tx_encolar(uint8_t c) {

    // Evitar que la ISR UART modifique tx_tail mientras estamos actualizando el buffer
    uint32_t estado_irq = save_and_disable_interrupts();

    // Saber si la cola estaba vacía antes de agregar el dato
    bool cola_vacia = (tx_head == tx_tail);

    // Calcular siguiente posición del buffer circular
    uint16_t siguiente = (tx_head + 1) % TX_BUF_SIZE;

    // Si siguiente != tail, existe espacio
    if (siguiente != tx_tail) {
        // Guardar carácter
        tx_buffer[tx_head] = c;
        // Avanzar head
        tx_head = siguiente;


        // Si la cola estaba completamente vacía, mandamos directamente el primer byte al UART.
        if (cola_vacia && uart_is_writable(UART_ID)) {
            uart_get_hw(UART_ID)->dr = tx_buffer[tx_tail];
            tx_tail = (tx_tail + 1) % TX_BUF_SIZE;
        }

        // Si quedan datos habilitar la IRQ de TX
        if (tx_tail != tx_head) {
            uart_set_irq_enables(UART_ID,true,true);
        }
        else {
            uart_set_irq_enables(UART_ID,true,false);
        }
    }
    // Restaurar interrupciones
    restore_interrupts(estado_irq);
}

// Encolar texto
static void tx_encolar_texto(const char *texto) {
    while (*texto != '\0') {
        tx_encolar((uint8_t)*texto);
        texto++;
    }
}

// ISR UART
static void on_uart_irq(void) {

    // Recepción
    while (uart_is_readable(UART_ID)) {

        // Leer registro de datos UART
        uint32_t dr = uart_get_hw(UART_ID)->dr;

        // Revisar framing error
        if (dr & UART_UARTDR_FE_BITS) {
            framing_errors++;
        }

        // Revisar overrun
        if (dr & UART_UARTDR_OE_BITS) {
            overruns++;
        }

        // Obtener únicamente los 8 bits del dato
        uint8_t c = (uint8_t)(dr & 0xFFu);

        // Calcular siguiente posición
        uint16_t siguiente = (rx_head + 1) % RX_BUF_SIZE;

        // Si existe espacio
        if (siguiente != rx_tail) {
            rx_buffer[rx_head] = c;
            rx_head = siguiente;
        }

        // Buffer circular lleno
        else {
            descartados++;
        }
    }

    // Transmisión
    while (uart_is_writable(UART_ID) && tx_tail != tx_head) {
        uart_get_hw(UART_ID)->dr = tx_buffer[tx_tail];
        tx_tail = (tx_tail + 1) % TX_BUF_SIZE;
    }

    // Si ya no queda nada por transmitir
    if (tx_tail == tx_head) {
        uart_set_irq_enables(UART_ID,true,false);
    }
}


// Callback del timer
static bool callback_juego(struct repeating_timer *t) {
    (void)t;

    // ESTADO_PREPARADOS
    if (estado_juego == PREPARADOS) {

        // Invertir estado del LED rojo
        rojo_encendido =!rojo_encendido;
        gpio_put(LED_ROJO,rojo_encendido);

        // Contar una ejecución del timer
        contador_timer++;

        // Si se cumpl el tiempo aleatorio...
        if ( contador_timer >= cambios_objetivo) {

            // Configuración LEDs
            rojo_encendido = false;
            gpio_put(LED_ROJO,0);
            gpio_put( LED_VERDE,1);

            // Tomar timestamp del inicio del verde
            t_inicio_verde = time_us_64();

            // Cambiar estado del juego
            estado_juego = YA;

            // Reiniciar contador PARA LOS 5s.
            contador_timer = 0;
        }

        // true significa que el timer continúa
        return true;
    }

    // ESTADO_YA
    if (estado_juego == YA) {
        contador_timer++;

        // Calcular callbacks para representar 5s.
        uint32_t ciclos_5s = (VENTANA_VERDE_MS + periodo_timer_actual_ms - 1u) / periodo_timer_actual_ms;

        // Verde + de 5s.
        if (contador_timer >= ciclos_5s) {
            // Apagar LED verde
            gpio_put(LED_VERDE,0);

            // Guardar resultado
            resultado = SIN_GANADOR;
            ganador = 0;
            jugador_falso = 0;
            tiempo_reaccion_us = 0;

            // Regresar al estado inicial
            estado_juego = DETENIDO;
            timer_activo = false;

            resultado_pendiente = true;

            // false detiene el repeating timer
            return false;
        }
        return true;
    }

    // ESTADO_DETENIDO
    timer_activo = false;
    return false;
}

// Función <start>
static bool iniciar_partida(void) {

    // Reinicio de datos
    numero_ronda++;
    ganador = 0;
    jugador_falso = 0;
    tiempo_reaccion_us = 0;

    // Configuración LEDs
    gpio_put(LED_VERDE,0);
    rojo_encendido = true;
    gpio_put( LED_ROJO,1);

    // Copiar periodo seteado
    periodo_timer_actual_ms = periodo_led_ms;

    // Calcular el minimo de callbacks equivalente a 2s.
    uint32_t min_cambios = (ESPERA_MIN_MS + periodo_timer_actual_ms - 1u) / periodo_timer_actual_ms;

    // Calcular el maximo de callbacks equivalente a 5s.
    uint32_t max_cambios = ESPERA_MAX_MS / periodo_timer_actual_ms;

    // Seguridad
    if (max_cambios < min_cambios) {
        max_cambios = min_cambios;
    }

    // Tiempo aleatorio
    cambios_objetivo = min_cambios + (rand()%(max_cambios - min_cambios + 1u));
    contador_timer = 0;             // Reiniciar contador
    estado_juego = PREPARADOS;      // Solo nos interesa el flanco de bajada


    // Timer repetitivo
    timer_activo = add_repeating_timer_ms(-(int32_t)periodo_timer_actual_ms,callback_juego,NULL,&timer_juego);

    // Si por alguna razón no pudo iniciarse el timer
    if (!timer_activo) {
        gpio_put(LED_ROJO,0);
        gpio_put(LED_VERDE,0);
        rojo_encendido = false;
        estado_juego = DETENIDO;
        return false;
    }
    return true;
}


// ISR de los botones
static void botones_isr(uint gpio,uint32_t events) {
    if (!(events & GPIO_IRQ_EDGE_FALL)) {       // Solo el flanco de bajada
        return;
    }
    uint64_t ahora = time_us_64();      // Tomar eltimestamp
    uint8_t jugador;
    if (gpio == BOTON_J1) {              // Identificar el botón del J1
        jugador = 1;
        if (ultimo_j1_us != 0 && ahora - ultimo_j1_us < DEBOUNCE_US) {      // Antirrebote de 20 ms
            return;
        }
        ultimo_j1_us = ahora;
    }
    else if (gpio == BOTON_J2) {         // Identificar el botón del J2
        jugador = 2;
        if (ultimo_j2_us != 0 && ahora - ultimo_j2_us < DEBOUNCE_US) {      // Antirrebote de 20 ms
            return;
        }
        ultimo_j2_us = ahora;
    }
    else {
        return;
    }

    // ESTADO_DETENIDO
    if (estado_juego == DETENIDO) {
        return;
    }

    // ESTAO_PREPARADOS
    if (estado_juego == PREPARADOS) {
        // Detener timer
        if (timer_activo) {
            cancel_repeating_timer(&timer_juego);
            timer_activo =false;
        }

        // Apagar LEDs
        gpio_put(LED_ROJO,0);
        gpio_put(LED_VERDE,0);
        rojo_encendido =false;

        // Guardar jugador que se adelantó
        jugador_falso = jugador;
        if (jugador == 1) {
            ganador = 2;
            score_j2++;
        }
        else {
            ganador = 1;
            score_j1++;
        }
        resultado = FALSO;

        // No existe tiempo de reacción válido
        tiempo_reaccion_us = 0;

        // Terminar partida
        estado_juego = DETENIDO;
        resultado_pendiente = true;
        return;
    }

    // ESTADO_YA
    if (estado_juego == YA) {

        // Detener timer
        if (timer_activo) {
            cancel_repeating_timer(&timer_juego);
            timer_activo = false;
        }

        // Apagar LEDs
        gpio_put(LED_VERDE,0);
        gpio_put(LED_ROJO,0);
        rojo_encendido = false;

        // Guardar ganador
        ganador = jugador;
        jugador_falso = 0;

        // Medición del tiempo de reacción
        tiempo_reaccion_us = ahora - t_inicio_verde;
        resultado = GANA;

        // Actualizar score
        if (jugador == 1) {
            score_j1++;
        }
        else {
            score_j2++;
        }

        // Terminar partida
        estado_juego = DETENIDO;
        resultado_pendiente = true;
    }
}

// Función <get stats>
static void mostrar_stats(void) {
    uint32_t estado_irq = save_and_disable_interrupts();
    uint32_t periodo = periodo_led_ms;
    estado_resultado_t r = resultado;
    uint8_t g = ganador;
    uint8_t jf = jugador_falso;
    uint64_t tiempo = tiempo_reaccion_us;
    uint32_t s1 = score_j1;
    uint32_t s2 = score_j2;
    uint32_t o = overruns;
    uint32_t f = framing_errors;
    uint32_t d = descartados;
    restore_interrupts(estado_irq);

    // Configuración periodo
    char mensaje[160];
    snprintf(
        mensaje,
        sizeof(mensaje),
        " Periodo configurado: %lu ms\r\n",
        (unsigned long)periodo
    );
    tx_encolar_texto(mensaje);
    tx_encolar_texto("Rango permitido: 50-1000 ms\r\n");
    tx_encolar_texto("Espera aleatoria: 2000-5000 ms\r\n");

    // Ultima partida - Resultados
    if (r == SIN_PARTIDAS) {
        tx_encolar_texto("Ultima partida: Sin partidas | tiempo=N/A\r\n");
    }
    else if (r == GANA) {
        uint64_t ms = tiempo / 1000ULL;
        uint64_t resto_us = tiempo % 1000ULL;

        snprintf(
            mensaje,
            sizeof(mensaje),
            "Ultima partida: ganador=J%u tiempo=%llu.%03llu ms\r\n",
            g,
            (unsigned long long)ms,
            (unsigned long long)resto_us
        );
        tx_encolar_texto(mensaje);
    }
    else if (r == FALSO) {
        snprintf(
            mensaje,
            sizeof(mensaje),
            "Ultima partida: ganador=J%u (salida en falso J%u) tiempo=N/A\r\n",
            g,
            jf
        );
        tx_encolar_texto(mensaje);
    }
    else if (r == SIN_GANADOR) {
        tx_encolar_texto("Ultima partida: Sin ganador | tiempo=N/A\r\n");
    }

    // Errores UART
    snprintf(
        mensaje,
        sizeof(mensaje),
        "UART: overruns=%lu framing_errors=%lu descartados=%lu\r\n",
        (unsigned long)o,
        (unsigned long)f,
        (unsigned long)d
    );

    tx_encolar_texto(mensaje);

    // Puntajes
    snprintf(
        mensaje,
        sizeof(mensaje),
        "Score: J1 %lu, J2 %lu\r\n",
        (unsigned long)s1,
        (unsigned long)s2
    );
    tx_encolar_texto(mensaje);
}

// Mostrar automáticamente el resultado al terminar una partida
static void tarea_resultado(void) {

    // Si no hay resultado nuevo, no hacer nada
    if (!resultado_pendiente) {
        return;
    }

    // Copiar datos compartidos
    uint32_t estado_irq = save_and_disable_interrupts();
    estado_resultado_t r = resultado;
    uint8_t g = ganador;
    uint8_t jf = jugador_falso;
    uint64_t tiempo = tiempo_reaccion_us;
    uint32_t s1 = score_j1;
    uint32_t s2 = score_j2;

    resultado_pendiente = false;
    restore_interrupts(estado_irq);

    char mensaje[160];
    // Ganador normal
    if (r == GANA) {
        uint64_t ms = tiempo / 1000ULL;
        uint64_t resto_us = tiempo % 1000ULL;
        snprintf(
            mensaje,
            sizeof(mensaje),
            "\r\nGanador: J%u | Tiempo: %llu.%03llu ms\r\n",
            g,
            (unsigned long long)ms,
            (unsigned long long)resto_us
        );
        tx_encolar_texto(mensaje);
    }
    // Salida en falso
    else if (r == FALSO) {
        snprintf(
            mensaje,
            sizeof(mensaje),
            "\r\nGanador: J%u | Salida en falso J%u | Tiempo: N/A\r\n",
            g,
            jf
        );
        tx_encolar_texto(mensaje);
    }
    // Nadie ganó
    else if (r == SIN_GANADOR) {
        tx_encolar_texto("\r\nSin ganador | Tiempo: N/A\r\n");
    }
    // Mostrar score actualizado
    snprintf(
        mensaje,
        sizeof(mensaje),
        "Score: J1 %lu, J2 %lu\r\n",
        (unsigned long)s1,
        (unsigned long)s2
    );
    tx_encolar_texto(mensaje);
}

// Procesar comandos
static void procesar_comando(char *cmd) {

    // Get stats
    if (strcmp(cmd,"get stats") == 0) {
        mostrar_stats();
        return;
    }

    // Start
    if (strcmp(cmd,"start") == 0) {
        if (estado_juego == DETENIDO) {
            if (iniciar_partida()) {
                tx_encolar_texto(" Listo\r\n");
            }
            else {
                tx_encolar_texto(" Error: no se pudo iniciar timer\r\n");
            }
        }
        else {
            tx_encolar_texto(" Error: partida en curso\r\n"
            );
        }
        return;
    }

    // Reset puntaje
    if (strcmp(cmd,"reset") == 0) {
        uint32_t estado_irq = save_and_disable_interrupts();    //
        score_j1 = 0;
        score_j2 = 0;
        restore_interrupts(estado_irq);                         //
        tx_encolar_texto(" Listo\r\n");
        return;
    }

    // Set periodo
    if (strncmp(cmd,"set periodo",11) == 0) {

        // Candado partida en curso
        if (estado_juego != DETENIDO) {
            tx_encolar_texto(" Error: partida en curso\r\n");
            return;
        }

        // Asignar valor del periodo
        unsigned int nuevo_periodo;
        char extra;
        int encontrados =
            sscanf(
                cmd,
                "set periodo %u %c",
                &nuevo_periodo,
                &extra
            );
        if (encontrados == 1) {
            // Comprobar rango 50-1000 ms
            if (nuevo_periodo >= 50 && nuevo_periodo <= 1000) {
                periodo_led_ms = nuevo_periodo;
                tx_encolar_texto(" Listo\r\n");
            }
            else {
                tx_encolar_texto(" Error: periodo fuera de rango (50-1000 ms)\r\n");
            }
        }
        else {
            tx_encolar_texto(" Error: comando desconocido\r\n");
        }
        return;
    }
    // Comando desconocido
    tx_encolar_texto(" Error: comando desconocido\r\n");
}

// Tarea UART
static void tarea_uart(void) {

    // Vaciar completamente el buffer RX
    while (rx_tail != rx_head) {
        uint8_t c = rx_buffer[rx_tail];
        rx_tail = (rx_tail + 1) % RX_BUF_SIZE;

        // Eco
        tx_encolar(c);

        // Fin de linea
        if (c == '\r' || c == '\n') {
            linea[n] = '\0';
            // Evitar procesar línea vacía
            if (n > 0) {
                procesar_comando(linea);
            }
            n = 0;
        }

        // Seguir acumulando caracteres
        else {

            if (n < LINE_SIZE - 1) {
                linea[n] = (char)c;
                n++;
            }
        }
    }
}

int main(void) {

    // LEDs
    gpio_init(LED_ROJO);
    gpio_set_dir(LED_ROJO,GPIO_OUT);
    gpio_put(LED_ROJO,0);
    gpio_init(LED_VERDE);
    gpio_set_dir(LED_VERDE,GPIO_OUT);
    gpio_put(LED_VERDE,0);

    // Botones
    gpio_init(BOTON_J1);
    gpio_set_dir(BOTON_J1,GPIO_IN);
    gpio_pull_up(BOTON_J1);
    gpio_init(BOTON_J2);
    gpio_set_dir(BOTON_J2,GPIO_IN);
    gpio_pull_up(BOTON_J2);

    // Interrupcion por botones con callback
    gpio_set_irq_enabled_with_callback(BOTON_J1,GPIO_IRQ_EDGE_FALL,true,&botones_isr);

    // Habilitar interrupción por edge fall
    gpio_set_irq_enabled(BOTON_J2,GPIO_IRQ_EDGE_FALL,true);

    // Set UART
    gpio_set_function(TX_PIN,GPIO_FUNC_UART);
    gpio_set_function(RX_PIN,GPIO_FUNC_UART);

    // Inicialización UART
    uart_init(UART_ID,BAUD_RATE);

    // Configuración 8N1
    uart_set_format(UART_ID,8,1,UART_PARITY_NONE);

    // FIFO UART habilitada
    uart_set_fifo_enabled(UART_ID,true);

    // Interrupciones UART
    irq_set_exclusive_handler(UART0_IRQ,on_uart_irq);
    irq_set_enabled(UART0_IRQ,true);
    uart_set_irq_enables(UART_ID,true,false);

    // Semilla pseudoaleatoria
    srand((unsigned int)time_us_64());

    // Mensaje de instrucciones
    tx_encolar_texto(
        "\r\n"
        "       DUELO DE REACCION\r\n"
        "\r\n"
        "Comandos:\r\n"
        " start\r\n"
        " set periodo <50-1000>\r\n"
        " get stats\r\n"
        " reset\r\n"
        "\r\n"
        "!: Puedes configurar el periodo antes de start.\r\n"
        "Si no lo cambias, se utiliza 500 ms.\r\n"
        "\r\n"
        "Escribe start en la consola para iniciar el duelo.\r\n"
    );

    while (true) {
        tarea_uart();
        tarea_resultado();
    }
    return 0;
}