# Laboratorio 3 — Análisis Capa de Transporte y Sockets
**Infraestructura de Comunicaciones — Universidad de los Andes**

## Estructura del proyecto

```
lab3/
├── broker_tcp.c       # Broker central — versión TCP (con hilos)
├── publisher_tcp.c    # Publicador de eventos — versión TCP
├── subscriber_tcp.c   # Suscriptor de eventos — versión TCP
├── broker_udp.c       # Broker central — versión UDP (sin hilos)
├── publisher_udp.c    # Publicador de eventos — versión UDP
├── subscriber_udp.c   # Suscriptor de eventos — versión UDP
└── Makefile
```

## Compilación

```bash
make          # Compila todo
make tcp      # Solo versión TCP
make udp      # Solo versión UDP
make clean    # Elimina ejecutables
```

Requiere: `gcc`, `libpthread` (preinstalada en Ubuntu/Debian).

---

## Cómo ejecutar el sistema

### Versión TCP (puerto 8080)

Abra **5 terminales** en la misma máquina o en la misma red:

```bash
# Terminal 1 — Broker (iniciar primero)
./broker_tcp

# Terminal 2 — Suscriptor 1 (un partido)
./subscriber_tcp 127.0.0.1 "Colombia vs Brasil"

# Terminal 3 — Suscriptor 2 (dos partidos)
./subscriber_tcp 127.0.0.1 "Colombia vs Brasil" "Argentina vs Peru"

# Terminal 4 — Publicador 1
./publisher_tcp 127.0.0.1 "Colombia vs Brasil"

# Terminal 5 — Publicador 2
./publisher_tcp 127.0.0.1 "Argentina vs Peru"
```

### Versión UDP (puerto 9090)

```bash
# Terminal 1 — Broker UDP
./broker_udp

# Terminal 2 — Suscriptor 1
./subscriber_udp 127.0.0.1 "Colombia vs Brasil"

# Terminal 3 — Suscriptor 2
./subscriber_udp 127.0.0.1 "Colombia vs Brasil" "Argentina vs Peru"

# Terminal 4 — Publicador 1
./publisher_udp 127.0.0.1 "Colombia vs Brasil"

# Terminal 5 — Publicador 2
./publisher_udp 127.0.0.1 "Argentina vs Peru"
```

> **Nota:** En UDP, los suscriptores deben registrarse ANTES de que el
> publicador comience a enviar mensajes. De lo contrario, los mensajes
> enviados antes del registro se pierden (no hay buffering en UDP).

---

## Captura con Wireshark

```bash
# Captura TCP (puerto 8080)
sudo wireshark -i lo -f "tcp port 8080" -w tcp_pubsub.pcap

# Captura UDP (puerto 9090)
sudo wireshark -i lo -f "udp port 9090" -w udp_pubsub.pcap
```

O desde la interfaz gráfica de Wireshark, use los filtros:
- TCP: `tcp.port == 8080`
- UDP: `udp.port == 9090`

---

## Funciones de socket utilizadas y su propósito

| Función       | TCP / UDP | Descripción                                              |
|---------------|-----------|----------------------------------------------------------|
| `socket()`    | Ambos     | Crea el socket; SOCK_STREAM=TCP, SOCK_DGRAM=UDP          |
| `bind()`      | Ambos     | Asocia el socket a una IP y puerto locales               |
| `listen()`    | TCP       | Pone el socket en modo escucha de conexiones entrantes   |
| `accept()`    | TCP       | Acepta una conexión; realiza el handshake de 3 vías      |
| `connect()`   | TCP       | Inicia el handshake desde el cliente                     |
| `send()`      | TCP       | Envía datos; TCP garantiza entrega y orden               |
| `recv()`      | TCP       | Recibe datos; bloquea hasta que haya datos disponibles   |
| `sendto()`    | UDP       | Envía datagrama especificando dirección destino          |
| `recvfrom()`  | UDP       | Recibe datagrama y conoce la dirección del remitente     |
| `close()`     | Ambos     | Cierra el socket (TCP envía FIN para cerrar conexión)    |
| `setsockopt()`| TCP       | Configura SO_REUSEADDR para reutilizar puerto            |
| `getsockname()`| UDP      | Obtiene el puerto local asignado por el SO               |
| `pthread_create()`| TCP  | Crea un hilo por cada cliente conectado                  |
| `pthread_mutex_lock()`| TCP | Protege la lista de suscriptores del acceso concurrente|
| `select()`    | UDP sub   | Espera datos con timeout para detectar pérdida de ACK    |

---

## Protocolo de mensajes

Todos los mensajes son cadenas de texto terminadas en `\n`:

```
PUB\n                          → Registro de publicador
SUB|tema1,tema2\n              → Registro de suscriptor con temas
MSG|tema|contenido\n           → Evento enviado por publicador
[tema] contenido\n             → Evento reenviado por broker a suscriptores
ACK\n                          → Confirmación de registro (UDP)
```

---

## Dependencias externas

Este laboratorio **no usa librerías de sockets de terceros**. Todo se
implementa directamente con las llamadas al sistema POSIX estándar:
- `<sys/socket.h>` — llamadas socket, bind, etc.
- `<netinet/in.h>` — estructuras de direcciones IPv4
- `<arpa/inet.h>`  — conversión de IPs (inet_pton, inet_ntoa)
- `<pthread.h>`    — hilos POSIX (solo broker_tcp.c)
- `<sys/select.h>` — multiplexación I/O (subscriber_udp.c)

Todas son parte del estándar POSIX, incluidas en `glibc` (Ubuntu/Debian).
