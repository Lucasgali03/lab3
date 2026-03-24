# Makefile — Laboratorio 3: Análisis Capa de Transporte y Sockets
# ================================================================
# Compila los seis programas del sistema pub-sub en TCP y UDP.
#
# Uso:
#   make          → compila todo
#   make tcp      → solo versión TCP
#   make udp      → solo versión UDP
#   make clean    → elimina ejecutables

CC      = gcc
CFLAGS  = -Wall -Wextra -g -std=c11
LDFLAGS = -lpthread   # pthread solo necesario para broker_tcp

# Destinos principales
TCP_TARGETS = broker_tcp publisher_tcp subscriber_tcp
UDP_TARGETS = broker_udp publisher_udp subscriber_udp
ALL_TARGETS = $(TCP_TARGETS) $(UDP_TARGETS)

.PHONY: all tcp udp clean

all: $(ALL_TARGETS)
	@echo ""
	@echo "✅  Compilación exitosa. Ejecutables:"
	@ls -lh $(ALL_TARGETS)

tcp: $(TCP_TARGETS)
udp: $(UDP_TARGETS)

# ── Versión TCP ───────────────────────────────────────────────
broker_tcp: broker_tcp.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

publisher_tcp: publisher_tcp.c
	$(CC) $(CFLAGS) -o $@ $<

subscriber_tcp: subscriber_tcp.c
	$(CC) $(CFLAGS) -o $@ $<

# ── Versión UDP ───────────────────────────────────────────────
broker_udp: broker_udp.c
	$(CC) $(CFLAGS) -o $@ $<

publisher_udp: publisher_udp.c
	$(CC) $(CFLAGS) -o $@ $<

subscriber_udp: subscriber_udp.c
	$(CC) $(CFLAGS) -o $@ $<

# ── Limpieza ──────────────────────────────────────────────────
clean:
	rm -f $(ALL_TARGETS)
	@echo "🧹  Ejecutables eliminados."
