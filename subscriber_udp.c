/*
 * subscriber_udp.c — Suscriptor de eventos deportivos (versión UDP)
 * =================================================================
 * En UDP, el suscriptor debe:
 *   1. Hacer bind() a un puerto local (para que el broker sepa dónde enviar).
 *   2. Enviar su registro (SUB) al broker con sendto().
 *   3. Esperar un ACK del broker (enviado manualmente, ya que UDP no tiene ACK).
 *   4. Recibir datagramas con recvfrom() en un bucle.
 *
 * Observaciones importantes:
 *   • Si el broker reenvía un mensaje justo ANTES de que el suscriptor
 *     se registre, ese mensaje se pierde para siempre (UDP no lo almacena).
 *   • Dos mensajes enviados seguidos pueden llegar en orden distinto.
 *   • El suscriptor no sabe si el broker se cayó (no hay keepalive).
 *
 * Compilar: gcc -o subscriber_udp subscriber_udp.c
 * Ejecutar: ./subscriber_udp 127.0.0.1 "Argentina vs Peru"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>   /* Para timeout con select() */
#include <netinet/in.h>
#include <arpa/inet.h>

#define BROKER_PORT  9090
#define BUFFER_SIZE  1024
#define ACK_TIMEOUT  5    /* Segundos de espera para ACK del broker */

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ip_broker> <tema1> [tema2] ...\n", argv[0]);
        fprintf(stderr, "Ej:  %s 127.0.0.1 \"Argentina vs Peru\"\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *broker_ip = argv[1];

    /* ① Crear socket UDP */
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    /*
     * ② Hacer bind() a un puerto local.
     *
     *    En TCP, el SO asigna el puerto local automáticamente en connect().
     *    En UDP, si no hacemos bind(), el SO asigna un puerto efímero SOLO
     *    cuando enviamos; pero el broker necesita conocer ese puerto para
     *    enviarnos mensajes. Con bind(puerto=0) el SO elige el puerto y
     *    podemos consultarlo con getsockname().
     */
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family      = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port        = htons(0);   /* 0 = el SO elige */

    if (bind(sock_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind"); return EXIT_FAILURE;
    }

    /* Consultar el puerto asignado */
    socklen_t local_len = sizeof(local_addr);
    getsockname(sock_fd, (struct sockaddr *)&local_addr, &local_len);
    printf("[SUBSCRIBER UDP] Enlazado al puerto local %d\n",
           ntohs(local_addr.sin_port));

    /* ③ Configurar dirección del broker */
    struct sockaddr_in broker_addr;
    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port   = htons(BROKER_PORT);
    if (inet_pton(AF_INET, broker_ip, &broker_addr.sin_addr) <= 0) {
        fprintf(stderr, "IP inválida: %s\n", broker_ip);
        return EXIT_FAILURE;
    }

    /* ④ Construir mensaje de suscripción y enviarlo */
    char reg_msg[BUFFER_SIZE] = "SUB|";
    for (int i = 2; i < argc; i++) {
        strncat(reg_msg, argv[i], sizeof(reg_msg) - strlen(reg_msg) - 2);
        if (i < argc - 1)
            strncat(reg_msg, ",", sizeof(reg_msg) - strlen(reg_msg) - 1);
    }
    strncat(reg_msg, "\n", sizeof(reg_msg) - strlen(reg_msg) - 1);

    sendto(sock_fd, reg_msg, strlen(reg_msg), 0,
           (struct sockaddr *)&broker_addr, sizeof(broker_addr));

    printf("[SUBSCRIBER UDP] Suscripción enviada al broker %s:%d\n",
           broker_ip, BROKER_PORT);

    /* ⑤ Esperar ACK del broker con timeout (usando select())
     *
     *   select() permite esperar con un tiempo límite.
     *   Si el broker no responde en ACK_TIMEOUT segundos,
     *   asumimos que el datagrama SUB se perdió (demostración de UDP).
     */
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock_fd, &read_fds);
    struct timeval timeout = { ACK_TIMEOUT, 0 };

    int sel = select(sock_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (sel == 0) {
        fprintf(stderr, "[SUBSCRIBER UDP] ADVERTENCIA: Sin ACK del broker "
                "(el datagrama SUB pudo perderse). Reintente.\n");
        close(sock_fd);
        return EXIT_FAILURE;
    } else if (sel < 0) {
        perror("select"); close(sock_fd); return EXIT_FAILURE;
    }

    char ack_buf[32];
    recvfrom(sock_fd, ack_buf, sizeof(ack_buf) - 1, 0, NULL, NULL);
    printf("[SUBSCRIBER UDP] ACK recibido. Suscripción confirmada.\n");

    printf("[SUBSCRIBER UDP] Esperando actualizaciones...\n");
    printf("─────────────────────────────────────────────────────\n");

    /* ⑥ Bucle de recepción de datagramas
     *
     *   Cada recvfrom() recibe UN datagrama completo (al contrario de TCP
     *   donde recv() puede traer fragmentos o varios mensajes juntos).
     *   Esta es una ventaja de UDP: los límites de mensajes se preservan.
     *
     *   PERO si el broker envió 2 datagramas y el segundo llegó antes
     *   que el primero (raro en LAN, posible en WAN), los recibiremos
     *   en ese orden incorrecto sin saberlo.
     */
    char buffer[BUFFER_SIZE];
    int  msg_count     = 0;
    int  last_seq      = 0;   /* Para detectar desorden */
    int  out_of_order  = 0;

    while (1) {
        int bytes = recvfrom(sock_fd, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
        if (bytes < 0) { perror("recvfrom"); break; }
        buffer[bytes] = '\0';

        /* Timestamp */
        time_t     now = time(NULL);
        struct tm *t   = localtime(&now);
        char       ts[20];
        strftime(ts, sizeof(ts), "%H:%M:%S", t);

        msg_count++;

        /* Detectar desorden comparando número de secuencia */
        int   seq     = 0;
        char *seq_ptr = strstr(buffer, "[SEQ:");
        if (seq_ptr) sscanf(seq_ptr, "[SEQ:%d]", &seq);

        if (seq > 0 && seq < last_seq) {
            out_of_order++;
            printf("[%s] *** DESORDEN: esperaba >%d, llego %d: %s",
                   ts, last_seq, seq, buffer);
        } else {
            printf("[%s] MSG#%02d -> %s", ts, msg_count, buffer);
        }
        if (seq > last_seq) last_seq = seq;

        fflush(stdout);
    }

    printf("─────────────────────────────────────────────────────\n");
    printf("[SUBSCRIBER UDP] Total mensajes recibidos : %d\n", msg_count);
    printf("[SUBSCRIBER UDP] Mensajes fuera de orden  : %d\n", out_of_order);

    close(sock_fd);
    return 0;
}
