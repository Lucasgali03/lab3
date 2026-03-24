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
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TCP_PORT    8080
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ip_broker> <tema1> [tema2] ...\n", argv[0]);
        fprintf(stderr, "Ej:  %s 127.0.0.1 \"Colombia vs Brasil\"\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *broker_ip = argv[1];

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

    if (send(sock_fd, reg_msg, strlen(reg_msg), 0) < 0) {
        perror("send suscripción");
        return EXIT_FAILURE;
    }

    printf("[SUBSCRIBER TCP] Suscrito a tema(s): ");
    for (int i = 2; i < argc; i++) printf("'%s' ", argv[i]);
    printf("\n");
    printf("[SUBSCRIBER TCP] Esperando actualizaciones...\n");
    printf("─────────────────────────────────────────────\n");

    /* ⑤ Recibir mensajes indefinidamente
     *
     *   NOTA sobre TCP y "message boundaries" (delimitación de mensajes):
     *   TCP es un protocolo de flujo de bytes (stream), no de mensajes.
     *   Esto significa que un recv() puede devolver datos de uno o varios
     *   mensajes juntos (si llegaron en el mismo segmento TCP) o solo parte
     *   de un mensaje. En este laboratorio, como los mensajes son cortos y
     *   enviados con sleep() entre ellos, en la práctica cada recv() trae
     *   un mensaje completo. En un sistema real, se necesitaría un parser
     *   que maneje este caso (por ejemplo, delimitar con longitud prefijada).
     */
    char buffer[BUFFER_SIZE];
    int  msg_count = 0;
    int  bytes_read;

    while ((bytes_read = recv(sock_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';

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
