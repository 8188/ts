CC = g++
CFLAGS = -std=c++17 -pthread -I.. -Wall -Wextra
LDFLAGS = -L.. -lredis++ -lhiredis -lmodbus -lspdlog

MQTT_LIB = -lpaho-mqttpp3 $(shell ./detect_mqtt.sh)

all: utils

utils: utils.cpp
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(MQTT_LIB)

debug: CFLAGS += -g
debug: utils

release: CFLAGS += -O3
release: utils

clean:
	rm -f utils

.PHONY: release debug clean