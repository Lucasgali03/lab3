/*
 * publisher_udp.c — Publicador de eventos deportivos (versión UDP)
 * ================================================================
 * Diferencias fundamentales respecto a publisher_tcp.c:
 *
 *  • NO hay connect() previo: cada sendto() envía el datagrama directamente.
 *  • NO hay confirmación de entrega: si el broker no recibe el datagrama,
 *    el publicador nunca lo sabe.
 *  • Menor latencia: sendto() retorna inmediatamente sin esperar ACK.
 *  • Los paquetes pueden llegar en distinto orden al broker si la red
 *    tiene rutas variables (raro en LAN, posible en WAN).
 *
 * Para evidenciar pérdida de orden/mensajes en Wireshark, enviamos los
 * mensajes con un número de secuencia explícito.
 *
 * Compilar: gcc -o publisher_udp publisher_udp.c
 * Ejecutar: ./publisher_udp 127.0.0.1 "Argentina vs Peru"
 */

#define _POSIX_C_SOURCE 200809L   /* Habilita usleep() en el estándar POSIX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define UDP_PORT    9090
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ip_broker> <tema_partido>\n", argv[0]);
        fprintf(stderr, "Ej:  %s 127.0.0.1 \"Argentina vs Peru\"\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *broker_ip = argv[1];
    const char *topic     = argv[2];

    /* ① Crear socket UDP
     *   SOCK_DGRAM: sin conexión, cada mensaje es un datagrama independiente.
     */
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    /* ② Configurar dirección del broker */
    struct sockaddr_in broker_addr;
    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port   = htons(UDP_PORT);
    if (inet_pton(AF_INET, broker_ip, &broker_addr.sin_addr) <= 0) {
        fprintf(stderr, "IP inválida: %s\n", broker_ip);
        return EXIT_FAILURE;
    }

    printf("[PUBLISHER UDP] Iniciado — tema: '%s' → broker %s:%d\n",
           topic, broker_ip, UDP_PORT);
    printf("[PUBLISHER UDP] ATENCIÓN: UDP no garantiza entrega ni orden.\n\n");

    /* ─── Eventos del partido ─────────────────────────────── */
    const char *events[] = {
        "[SEQ:01] Inicio del partido",
        "[SEQ:02] Gol de Argentina al minuto 8 — 1-0",
        "[SEQ:03] Fuera de lugar anulado al minuto 15",
        "[SEQ:04] Tarjeta amarilla al numero 6 de Peru",
        "[SEQ:05] Cambio en Peru: jugador 11 entra por jugador 9",
        "[SEQ:06] Gol de Peru al minuto 40 — Empate 1-1",
        "[SEQ:07] Fin del primer tiempo: Argentina 1 - Peru 1",
        "[SEQ:08] Inicio del segundo tiempo",
        "[SEQ:09] Gol de Argentina al minuto 55 — 2-1",
        "[SEQ:10] Tiro al palo de Peru al minuto 70",
        "[SEQ:11] Gol de Argentina al minuto 82 — 3-1",
        "[SEQ:12] Fin del partido: Argentina 3 - Peru 1"
    };
    int num_events = (int)(sizeof(events) / sizeof(events[0]));

    socklen_t broker_len = sizeof(broker_addr);
    char      buffer[BUFFER_SIZE];

    /* ③ Enviar eventos como datagramas UDP */
    for (int i = 0; i < num_events; i++) {
        int len = snprintf(buffer, sizeof(buffer), "MSG|%s|%s\n", topic, events[i]);

        /*
         * sendto() — envía el datagrama y retorna INMEDIATAMENTE.
         * No hay handshake, no hay confirmación, no hay control de flujo.
         * Si el broker está caído, el datagrama simplemente se pierde.
         */
        int sent = sendto(sock_fd, buffer, (size_t)len, 0,
                          (struct sockaddr *)&broker_addr, broker_len);
        if (sent < 0) {
            perror("sendto");
            /* En UDP, el error puede ser solo local (buffer lleno, etc.)
             * pero NO indica si el destino lo recibió. */
            continue;
        }

        printf("[PUBLISHER UDP] Datagrama #%02d enviado: %s\n", i + 1, events[i]);

        /* Esperar 500 ms entre mensajes para simular tiempo real */
        struct timespec ts_sleep = { 0, 500000000L };  /* 0s + 500ms en nanosegundos */
        nanosleep(&ts_sleep, NULL);
    }

    close(sock_fd);
    printf("\n[PUBLISHER UDP] Todos los datagramas enviados. Fin.\n");
    return 0;
}
