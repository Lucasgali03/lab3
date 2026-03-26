/*
 * subscriber_tcp.c — Suscriptor de eventos deportivos (versión TCP)
 * =================================================================
 * El suscriptor es un aficionado que quiere recibir actualizaciones
 * en tiempo real de uno o varios partidos.
 *
 * Flujo de trabajo:
 *   1. Conectar al broker mediante TCP.
 *   2. Registrarse enviando "SUB|tema1,tema2\n".
 *   3. Esperar y mostrar los mensajes que lleguen del broker.
 *
 * Por qué TCP garantiza la experiencia:
 *   recv() no retorna hasta que haya datos disponibles. TCP retransmite
 *   paquetes perdidos, por lo que nunca se pierden actualizaciones.
 *   Los mensajes llegan siempre en el mismo orden que fueron enviados.
 *
 * Compilar: gcc -o subscriber_tcp subscriber_tcp.c
 * Ejecutar: ./subscriber_tcp 127.0.0.1 "Colombia vs Brasil"
 *           ./subscriber_tcp 127.0.0.1 "Colombia vs Brasil" "Argentina vs Peru"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TCP_PORT    8080
#define BUFFER_SIZE 1024

static ssize_t recv_line(int fd, char *buffer, size_t size) {
    if (size == 0) return -1;

    size_t total = 0;
    while (total < size - 1) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n == 0) {
            if (total == 0) return 0;
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        buffer[total++] = ch;
        if (ch == '\n') break;
    }

    buffer[total] = '\0';
    return (ssize_t)total;
}

static int send_all(int fd, const char *buffer, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t sent = send(fd, buffer + total, len - total, 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += (size_t)sent;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ip_broker> <tema1> [tema2] ...\n", argv[0]);
        fprintf(stderr, "Ej:  %s 127.0.0.1 \"Colombia vs Brasil\"\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *broker_ip = argv[1];

    signal(SIGPIPE, SIG_IGN);

    /* ① Crear socket TCP */
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    /* ② Configurar dirección del broker */
    struct sockaddr_in broker_addr;
    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port   = htons(TCP_PORT);
    if (inet_pton(AF_INET, broker_ip, &broker_addr.sin_addr) <= 0) {
        fprintf(stderr, "IP inválida: %s\n", broker_ip);
        return EXIT_FAILURE;
    }

    /* ③ Conectar al broker (handshake TCP) */
    if (connect(sock_fd, (struct sockaddr *)&broker_addr, sizeof(broker_addr)) < 0) {
        perror("connect");
        return EXIT_FAILURE;
    }
    printf("[SUBSCRIBER TCP] Conectado al broker %s:%d\n", broker_ip, TCP_PORT);

    /* ④ Construir y enviar mensaje de suscripción
     *   Formato: "SUB|tema1,tema2,tema3\n"
     */
    char reg_msg[BUFFER_SIZE] = "SUB|";
    for (int i = 2; i < argc; i++) {
        strncat(reg_msg, argv[i], sizeof(reg_msg) - strlen(reg_msg) - 2);
        if (i < argc - 1)
            strncat(reg_msg, ",", sizeof(reg_msg) - strlen(reg_msg) - 1);
    }
    strncat(reg_msg, "\n", sizeof(reg_msg) - strlen(reg_msg) - 1);

    if (send_all(sock_fd, reg_msg, strlen(reg_msg)) < 0) {
        perror("send suscripción");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("[SUBSCRIBER TCP] Suscrito a tema(s): ");
    for (int i = 2; i < argc; i++) printf("'%s' ", argv[i]);
    printf("\n");
    printf("[SUBSCRIBER TCP] Esperando actualizaciones...\n");
    printf("─────────────────────────────────────────────\n");

    /* ⑤ Recibir mensajes indefinidamente
     *
     *   Como el protocolo del laboratorio usa '\n' como delimitador,
     *   leemos hasta completar cada línea antes de procesarla. Así no
     *   dependemos de que recv() coincida casualmente con los límites
     *   lógicos del mensaje.
     */
    char buffer[BUFFER_SIZE];
    int  msg_count = 0;
    ssize_t bytes_read;

    while ((bytes_read = recv_line(sock_fd, buffer, sizeof(buffer))) > 0) {
        /* Obtener marca de tiempo actual */
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", t);

        msg_count++;
        printf("[%s] MSG#%02d → %s", timestamp, msg_count, buffer);
        fflush(stdout);
    }

    if (bytes_read == 0) {
        printf("─────────────────────────────────────────────\n");
        printf("[SUBSCRIBER TCP] El broker cerró la conexión.\n");
    } else {
        perror("recv");
    }

    printf("[SUBSCRIBER TCP] Total mensajes recibidos: %d\n", msg_count);
    close(sock_fd);
    return 0;
}
