/*
 * broker_tcp.c — Broker de mensajes usando TCP
 * =============================================
 * El broker es el componente central del sistema pub-sub. Actúa como intermediario:
 *   1. Acepta conexiones de publicadores (que envían eventos de partidos).
 *   2. Acepta conexiones de suscriptores (que se registran para recibir eventos).
 *   3. Reenvía cada mensaje del publicador a todos los suscriptores interesados en ese tema.
 *
 * Por qué usamos hilos (pthreads):
 *   TCP es orientado a conexión; cada cliente ocupa un socket activo. Para atender
 *   múltiples clientes simultáneamente sin bloquearnos en uno solo, creamos un hilo
 *   por cada conexión entrante.
 *
 * Protocolo de mensajes definido:
 *   - Registro de publicador : "PUB\n"
 *   - Registro de suscriptor : "SUB|tema1,tema2\n"
 *   - Mensaje del publicador : "MSG|tema|contenido\n"
 *
 * Compilar: gcc -o broker_tcp broker_tcp.c -lpthread
 * Ejecutar: ./broker_tcp
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>   /* socket(), bind(), listen(), accept() */
#include <netinet/in.h>   /* struct sockaddr_in, htons() */
#include <arpa/inet.h>    /* inet_ntoa() */

/* ─── Constantes ──────────────────────────────────────────── */
#define TCP_PORT      8080   /* Puerto en el que escucha el broker            */
#define MAX_CLIENTS   100    /* Máximo de clientes simultáneos                */
#define BUFFER_SIZE   1024   /* Tamaño del buffer de lectura/escritura        */
#define MAX_TOPICS    10     /* Máx. temas por suscriptor                     */
#define MAX_TOPIC_LEN 128    /* Longitud máxima de un nombre de tema          */

/* ─── Estructura que representa un suscriptor conectado ─────── */
typedef struct {
    int  socket_fd;                        /* Descriptor del socket TCP        */
    char topics[MAX_TOPICS][MAX_TOPIC_LEN];/* Lista de temas suscritos         */
    int  topic_count;                      /* Cuántos temas tiene              */
    int  active;                           /* 1 = conectado, 0 = desconectado  */
} Subscriber;

/* ─── Variables globales compartidas entre hilos ─────────────── */
Subscriber      subscribers[MAX_CLIENTS];
int             subscriber_count = 0;
pthread_mutex_t sub_mutex = PTHREAD_MUTEX_INITIALIZER; /* Protege la lista */

/* ────────────────────────────────────────────────────────────────
 * parse_subscription()
 * Convierte "SUB|Colombia vs Brasil,Argentina vs Peru" en la lista
 * de temas dentro de la estructura Subscriber.
 * ──────────────────────────────────────────────────────────────── */
void parse_subscription(const char *msg, Subscriber *sub) {
    /* Avanzar más allá de "SUB|" */
    const char *topics_part = strchr(msg, '|');
    if (!topics_part) return;
    topics_part++;

    char copy[BUFFER_SIZE];
    strncpy(copy, topics_part, BUFFER_SIZE - 1);
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
 * forward_to_subscribers()
 * Recorre la lista de suscriptores activos y envía el mensaje a
 * aquellos que estén suscritos al tema indicado.
 * Usa el mutex para evitar condiciones de carrera al leer la lista.
 * ──────────────────────────────────────────────────────────────── */
void forward_to_subscribers(const char *topic, const char *content) {
    char out[BUFFER_SIZE];
    snprintf(out, sizeof(out), "[%s] %s\n", topic, content);

    pthread_mutex_lock(&sub_mutex);   /* Bloquear antes de leer la lista */

    int forwarded = 0;
    for (int i = 0; i < subscriber_count; i++) {
        if (!subscribers[i].active) continue;

        for (int j = 0; j < subscribers[i].topic_count; j++) {
            if (strcmp(subscribers[i].topics[j], topic) == 0) {
                /*
                 * send() en TCP garantiza que el mensaje llegue completo
                 * y en orden. Si el buffer del receptor está lleno, TCP
                 * bloquea al emisor (control de flujo).
                 */
                int sent = send(subscribers[i].socket_fd, out, strlen(out), 0);
                if (sent < 0) {
                    /* Si falló el envío, marcamos el suscriptor como inactivo */
                    subscribers[i].active = 0;
                    printf("[BROKER] Suscriptor fd=%d desconectado inesperadamente.\n",
                           subscribers[i].socket_fd);
                } else {
                    forwarded++;
                }
                break; /* No enviar más de una vez al mismo suscriptor */
            }
        }
    }

    pthread_mutex_unlock(&sub_mutex);

    printf("[BROKER] Mensaje reenviado a %d suscriptor(es).\n", forwarded);
}

/* ────────────────────────────────────────────────────────────────
 * handle_client()  — función ejecutada por cada hilo
 * El primer mensaje determina si el cliente es PUB o SUB.
 * ──────────────────────────────────────────────────────────────── */
void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);  /* El fd fue reservado en el heap; liberamos aquí */

    char buffer[BUFFER_SIZE];
    int  n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) { close(client_fd); return NULL; }
    buffer[n] = '\0';

    /* ── Caso 1: Es un publicador ── */
    if (strncmp(buffer, "PUB", 3) == 0) {
        printf("[BROKER] Publicador conectado (fd=%d)\n", client_fd);

        while ((n = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[n] = '\0';

            /* Formato esperado: "MSG|tema|contenido\n" */
            if (strncmp(buffer, "MSG|", 4) == 0) {
                char *p_topic   = buffer + 4;
                char *p_content = strchr(p_topic, '|');
                if (!p_content) continue;

                *p_content = '\0';   /* Separar tema del contenido */
                p_content++;

                /* Eliminar salto de línea al final */
                char *nl = strpbrk(p_content, "\r\n");
                if (nl) *nl = '\0';

                printf("[BROKER] MSG recibido — tema='%s' contenido='%s'\n",
                       p_topic, p_content);

                forward_to_subscribers(p_topic, p_content);
            }
        }
        printf("[BROKER] Publicador desconectado (fd=%d)\n", client_fd);

    /* ── Caso 2: Es un suscriptor ── */
    } else if (strncmp(buffer, "SUB|", 4) == 0) {
        pthread_mutex_lock(&sub_mutex);

        int idx = subscriber_count++;
        subscribers[idx].socket_fd  = client_fd;
        subscribers[idx].active     = 1;
        parse_subscription(buffer, &subscribers[idx]);

        printf("[BROKER] Suscriptor conectado (fd=%d) — temas: ", client_fd);
        for (int i = 0; i < subscribers[idx].topic_count; i++)
            printf("'%s' ", subscribers[idx].topics[i]);
        printf("\n");

        pthread_mutex_unlock(&sub_mutex);

        /*
         * El suscriptor no envía más mensajes; simplemente mantenemos
         * la conexión abierta. recv() bloqueará hasta que el cliente
         * se desconecte o envíe algo.
         */
        while ((n = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0);

        pthread_mutex_lock(&sub_mutex);
        subscribers[idx].active = 0;
        pthread_mutex_unlock(&sub_mutex);

        printf("[BROKER] Suscriptor desconectado (fd=%d)\n", client_fd);

    } else {
        printf("[BROKER] Mensaje desconocido de fd=%d: '%s'\n", client_fd, buffer);
    }

    close(client_fd);
    return NULL;
}

/* ─── main() ──────────────────────────────────────────────────── */
int main(void) {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    /* ① Crear el socket TCP
     *   AF_INET   = familia de direcciones IPv4
     *   SOCK_STREAM = tipo orientado a conexión (TCP)
     *   0         = protocolo por defecto para SOCK_STREAM (TCP)
     */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    /* ② Permitir reutilizar la dirección inmediatamente tras reiniciar */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* ③ Configurar la dirección del servidor */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;          /* Escuchar en todas las IPs */
    server_addr.sin_port        = htons(TCP_PORT);     /* htons: host-to-network byte order */

    /* ④ Asociar (bind) el socket a la dirección/puerto */
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(EXIT_FAILURE);
    }

    /* ⑤ Poner el socket en modo escucha (listen)
     *   El segundo parámetro (10) es el backlog: cuántas conexiones
     *   pendientes puede encolar el SO antes de que las aceptemos.
     */
    if (listen(server_fd, 10) < 0) { perror("listen"); exit(EXIT_FAILURE); }

    printf("╔══════════════════════════════════════╗\n");
    printf("║  BROKER TCP iniciado en puerto %d  ║\n", TCP_PORT);
    printf("╚══════════════════════════════════════╝\n");

    /* ⑥ Bucle principal: aceptar conexiones y crear hilos */
    while (1) {
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) { perror("malloc"); continue; }

        /*
         * accept() bloquea hasta que llega una conexión.
         * TCP ejecuta el handshake de 3 vías (SYN → SYN-ACK → ACK)
         * antes de retornar aquí. Eso es lo que Wireshark mostrará.
         */
        *client_fd = accept(server_fd,
                            (struct sockaddr *)&client_addr,
                            &client_len);
        if (*client_fd < 0) {
            perror("accept"); free(client_fd); continue;
        }

        printf("[BROKER] Nueva conexión desde %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        /* Crear hilo para manejar este cliente */
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_fd) != 0) {
            perror("pthread_create"); free(client_fd);
        }
        pthread_detach(tid); /* El hilo libera recursos al terminar solo */
    }

    close(server_fd);
    return 0;
}
