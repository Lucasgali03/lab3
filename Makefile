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

# Compatibilidad básica Unix/Windows
ifeq ($(OS),Windows_NT)
EXE_EXT = .exe
RM_CMD  = cmd /C del /Q
else
EXE_EXT =
RM_CMD  = rm -f
endif

# Programas y destinos principales
PROGRAMS    = broker_tcp publisher_tcp subscriber_tcp broker_udp publisher_udp subscriber_udp
TCP_TARGETS = broker_tcp$(EXE_EXT) publisher_tcp$(EXE_EXT) subscriber_tcp$(EXE_EXT)
UDP_TARGETS = broker_udp$(EXE_EXT) publisher_udp$(EXE_EXT) subscriber_udp$(EXE_EXT)
ALL_TARGETS = $(addsuffix $(EXE_EXT),$(PROGRAMS))

.PHONY: all tcp udp clean

all: $(ALL_TARGETS)
	@echo ""
	@echo "Compilación exitosa. Ejecutables: $(ALL_TARGETS)"

tcp: $(TCP_TARGETS)
udp: $(UDP_TARGETS)

# broker_tcp requiere pthread; el resto usa la regla patrón.
broker_tcp$(EXE_EXT): broker_tcp.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

%$(EXE_EXT): %.c
	$(CC) $(CFLAGS) -o $@ $<

# ── Limpieza ──────────────────────────────────────────────────
clean:
	-$(RM_CMD) $(ALL_TARGETS)
	@echo "Ejecutables eliminados."
