/*
 * broker_udp.c — Broker de mensajes usando UDP
 * =============================================
 * Diferencias clave respecto al broker TCP:
 *
 *  • No hay conexiones persistentes. Cada datagrama llega de forma
 *    independiente; el broker identifica al remitente por su IP:puerto.
 *  • No se necesitan hilos: un único bucle recvfrom() procesa todos los
 *    mensajes secuencialmente. UDP tiene menos overhead.
 *  • Los suscriptores deben registrarse enviando un datagrama SUB; el
 *    broker guarda su dirección (IP + puerto) para enviarles mensajes.
 *  • No hay garantía de entrega: si un paquete se pierde en la red,
 *    nadie lo reenvía.
 *
 * Protocolo de mensajes (mismo que TCP pero como datagramas):
 *   - Registro suscriptor: "SUB|tema1,tema2\n"
 *   - Mensaje publicador : "MSG|tema|contenido\n"
 *   - ACK al suscriptor  : "ACK\n"
 *
 * Compilar: gcc -o broker_udp broker_udp.c
 * Ejecutar: ./broker_udp
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ─── Constantes ──────────────────────────────────────────── */
#define UDP_PORT      9090
#define MAX_SUBS      100
#define MAX_TOPICS    10
#define MAX_TOPIC_LEN 128
#define BUFFER_SIZE   1024

/* ─── Estructura para registrar suscriptores UDP ─────────── */
typedef struct {
    struct sockaddr_in addr;                  /* Dirección (IP + puerto) del suscriptor */
    char  topics[MAX_TOPICS][MAX_TOPIC_LEN];  /* Temas suscritos                        */
    int   topic_count;                        /* Cantidad de temas                      */
    int   active;                             /* 1 = activo                             */
} UDPSubscriber;

UDPSubscriber subscribers[MAX_SUBS];
int           sub_count = 0;

/* ────────────────────────────────────────────────────────────────
 * parse_topics()
 * Extrae los temas de una cadena "tema1,tema2,tema3" y los guarda
 * en la estructura UDPSubscriber.
 * ──────────────────────────────────────────────────────────────── */
void parse_topics(const char *topics_str, UDPSubscriber *sub) {
    char copy[BUFFER_SIZE];
    strncpy(copy, topics_str, BUFFER_SIZE - 1);
    copy[BUFFER_SIZE - 1] = '\0';

    sub->topic_count = 0;
    char *token = strtok(copy, ",\n\r");
    while (token && sub->topic_count < MAX_TOPICS) {
        strncpy(sub->topics[sub->topic_count], token, MAX_TOPIC_LEN - 1);
        sub->topics[sub->topic_count][MAX_TOPIC_LEN - 1] = '\0';
        sub->topic_count++;
        token = strtok(NULL, ",\n\r");
    }
}

/* ────────────────────────────────────────────────────────────────
 * find_or_add_subscriber()
 * Busca si ya existe un suscriptor con esa dirección; si no, agrega uno nuevo.
 * Retorna el índice en el arreglo.
 * ──────────────────────────────────────────────────────────────── */
int find_or_add_subscriber(struct sockaddr_in *addr) {
    for (int i = 0; i < sub_count; i++) {
        if (subscribers[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            subscribers[i].addr.sin_port        == addr->sin_port) {
            return i;  /* Ya existe */
        }
    }
    if (sub_count >= MAX_SUBS) {
        fprintf(stderr, "[BROKER UDP] ¡Límite de suscriptores alcanzado!\n");
        return -1;
    }
    return sub_count++;  /* Nuevo slot */
}

/* ─── main() ──────────────────────────────────────────────────── */
int main(void) {
    /* ① Crear socket UDP
     *   SOCK_DGRAM = datagrama, no orientado a conexión.
     *   No hay handshake; cada paquete es independiente.
     */
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    /* ② Configurar dirección y hacer bind */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(UDP_PORT);

    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); return EXIT_FAILURE;
    }

    printf("╔══════════════════════════════════════╗\n");
    printf("║  BROKER UDP iniciado en puerto %d  ║\n", UDP_PORT);
    printf("╚══════════════════════════════════════╝\n");
    printf("[BROKER UDP] Sin hilos: procesamiento secuencial de datagramas.\n\n");

    char               buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t          client_len = sizeof(client_addr);

    /* ③ Bucle principal: recibir y procesar datagramas
     *
     *   recvfrom() — versión UDP de recv() que también devuelve la
     *   dirección del remitente. Es fundamental para UDP porque no
     *   existe "conexión" que identifique al cliente.
     */
    while (1) {
        int bytes = recvfrom(sock_fd, buffer, sizeof(buffer) - 1, 0,
                             (struct sockaddr *)&client_addr, &client_len);
        if (bytes < 0) { perror("recvfrom"); continue; }
        buffer[bytes] = '\0';

        /* ── SUB: Registro de suscriptor ── */
        if (strncmp(buffer, "SUB|", 4) == 0) {
            int idx = find_or_add_subscriber(&client_addr);
            if (idx < 0) continue;

            subscribers[idx].addr   = client_addr;
            subscribers[idx].active = 1;
            parse_topics(buffer + 4, &subscribers[idx]);

            printf("[BROKER UDP] Suscriptor registrado — %s:%d — temas: ",
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));
            for (int i = 0; i < subscribers[idx].topic_count; i++)
                printf("'%s' ", subscribers[idx].topics[i]);
            printf("\n");

            /*
             * Enviamos un ACK para que el suscriptor sepa que su
             * registro fue recibido. En UDP esto es manual; TCP lo
             * haría automáticamente con su mecanismo de ACK.
             */
            sendto(sock_fd, "ACK\n", 4, 0,
                   (struct sockaddr *)&client_addr, client_len);

        /* ── MSG: Mensaje de publicador ── */
        } else if (strncmp(buffer, "MSG|", 4) == 0) {
            char *p_topic   = buffer + 4;
            char *p_content = strchr(p_topic, '|');
            if (!p_content) {
                printf("[BROKER UDP] Formato de MSG inválido, ignorado.\n");
                continue;
            }
            *p_content = '\0';
            p_content++;

            char *nl = strpbrk(p_content, "\r\n");
            if (nl) *nl = '\0';

            printf("[BROKER UDP] MSG recibido — tema='%s' contenido='%s'\n",
                   p_topic, p_content);

            /* Reenviar a suscriptores interesados */
            char out[BUFFER_SIZE];
            snprintf(out, sizeof(out), "[%s] %s\n", p_topic, p_content);

            int forwarded = 0;
            for (int i = 0; i < sub_count; i++) {
                if (!subscribers[i].active) continue;
                for (int j = 0; j < subscribers[i].topic_count; j++) {
                    if (strcmp(subscribers[i].topics[j], p_topic) == 0) {
                        /*
                         * sendto() en UDP: envía el datagrama y retorna
                         * inmediatamente. No espera confirmación.
                         * Si el paquete se pierde, nadie lo retransmite.
                         */
                        sendto(sock_fd, out, strlen(out), 0,
                               (struct sockaddr *)&subscribers[i].addr,
                               sizeof(subscribers[i].addr));
                        forwarded++;
                        break;
                    }
                }
            }
            printf("[BROKER UDP] Datagrama enviado a %d suscriptor(es).\n", forwarded);

        /* ── UNSUB: Desregistro (opcional) ── */
        } else if (strncmp(buffer, "UNSUB", 5) == 0) {
            for (int i = 0; i < sub_count; i++) {
                if (subscribers[i].addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                    subscribers[i].addr.sin_port        == client_addr.sin_port) {
                    subscribers[i].active = 0;
                    printf("[BROKER UDP] Suscriptor %s:%d desregistrado.\n",
                           inet_ntoa(client_addr.sin_addr),
                           ntohs(client_addr.sin_port));
                    break;
                }
            }

        } else {
            printf("[BROKER UDP] Datagrama desconocido de %s:%d: '%s'\n",
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port), buffer);
        }
    }

    close(sock_fd);
    return 0;
}
