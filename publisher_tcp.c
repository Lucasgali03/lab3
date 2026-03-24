/*
 * publisher_tcp.c — Publicador de eventos deportivos (versión TCP)
 * ================================================================
 * El publicador representa a un periodista deportivo que cubre un partido
 * y envía actualizaciones al broker en tiempo real.
 *
 * Flujo de trabajo:
 *   1. Conectar al broker mediante TCP (handshake de 3 vías).
 *   2. Registrarse enviando "PUB\n".
 *   3. Enviar mensajes con formato "MSG|tema|contenido\n".
 *   4. TCP garantiza que cada mensaje llegue completo y en orden.
 *
 * Por qué TCP aquí:
 *   Cada send() espera confirmación (ACK) del receptor. Si la red pierde
 *   un paquete, TCP lo retransmite automáticamente. El publicador no se
 *   preocupa por la confiabilidad: TCP lo maneja por él.
 *
 * Compilar: gcc -o publisher_tcp publisher_tcp.c
 * Ejecutar: ./publisher_tcp 127.0.0.1 "Colombia vs Brasil"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TCP_PORT    8080
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ip_broker> <tema_partido>\n", argv[0]);
        fprintf(stderr, "Ej:  %s 127.0.0.1 \"Colombia vs Brasil\"\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *broker_ip = argv[1];
    const char *topic     = argv[2];

    /* ① Crear socket TCP */
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    /* ② Configurar dirección del broker */
    struct sockaddr_in broker_addr;
    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port   = htons(TCP_PORT);
    /*
     * inet_pton: convierte la cadena "127.0.0.1" al formato binario
     * de 32 bits que usa la red.
     */
    if (inet_pton(AF_INET, broker_ip, &broker_addr.sin_addr) <= 0) {
        fprintf(stderr, "Dirección IP inválida: %s\n", broker_ip);
        return EXIT_FAILURE;
    }

    /* ③ Conectar al broker
     *   Aquí TCP realiza el handshake de 3 vías:
     *     Cliente → SYN     → Broker
     *     Cliente ← SYN-ACK ← Broker
     *     Cliente → ACK     → Broker
     *   Visible en Wireshark como los primeros 3 paquetes.
     */
    if (connect(sock_fd, (struct sockaddr *)&broker_addr, sizeof(broker_addr)) < 0) {
        perror("connect");
        return EXIT_FAILURE;
    }
    printf("[PUBLISHER TCP] Conectado al broker %s:%d\n", broker_ip, TCP_PORT);

    /* ④ Registrarse como publicador */
    if (send(sock_fd, "PUB\n", 4, 0) < 0) { perror("send registro"); return EXIT_FAILURE; }
    printf("[PUBLISHER TCP] Registrado como publicador — tema: '%s'\n", topic);

    /* ─── Eventos del partido ──────────────────────────────── */
    /*
     * Cada evento incluye número de secuencia para que, en las capturas
     * de Wireshark, podamos verificar el orden de llegada.
     */
    const char *events[] = {
        "[SEQ:01] Inicio del partido",
        "[SEQ:02] Gol de Colombia al minuto 12 — 1-0",
        "[SEQ:03] Falta cometida por jugador 7 de Brasil",
        "[SEQ:04] Tarjeta amarilla al numero 10 de Brasil",
        "[SEQ:05] Cambio en Colombia: jugador 9 entra por jugador 11",
        "[SEQ:06] Gol de Brasil al minuto 34 — Empate 1-1",
        "[SEQ:07] Fin del primer tiempo: Colombia 1 - Brasil 1",
        "[SEQ:08] Inicio del segundo tiempo",
        "[SEQ:09] Gol de Colombia al minuto 67 — 2-1",
        "[SEQ:10] Tarjeta roja al numero 5 de Brasil — Brasil queda con 10",
        "[SEQ:11] Gol de Colombia al minuto 89 — 3-1",
        "[SEQ:12] Fin del partido: Colombia 3 - Brasil 1"
    };
    int num_events = (int)(sizeof(events) / sizeof(events[0]));

    /* ⑤ Enviar eventos uno a uno */
    char buffer[BUFFER_SIZE];
    for (int i = 0; i < num_events; i++) {
        /*
         * Formato: "MSG|tema|contenido\n"
         * El broker dividirá el mensaje usando el separador '|'.
         */
        int len = snprintf(buffer, sizeof(buffer), "MSG|%s|%s\n", topic, events[i]);
        if (len < 0 || len >= (int)sizeof(buffer)) {
            fprintf(stderr, "Mensaje demasiado largo, omitido.\n");
            continue;
        }

        int sent = send(sock_fd, buffer, (size_t)len, 0);
        if (sent < 0) {
            perror("send mensaje");
            break;
        }

        printf("[PUBLISHER] Enviado (%d/%d): %s\n", i + 1, num_events, events[i]);

        /* Esperar 1 segundo entre mensajes para simular tiempo real */
        sleep(1);
    }

    /* ⑥ Cerrar conexión
     *   TCP enviará FIN → ACK → FIN → ACK (4-way teardown).
     *   También visible en Wireshark.
     */
    close(sock_fd);
    printf("[PUBLISHER TCP] Desconectado del broker.\n");
    return 0;
}
