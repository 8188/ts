CXX = g++
CXXFLAGS = -pthread -std=c++17 -I.. -Wall -Wextra
LIBS = -L.. -lredis++ -lhiredis -lmodbus -lspdlog
MQTT_LIB = -lpaho-mqttpp3 $(shell ./detect_mqtt.sh)

OUT = main modbus_server

SRC_MAIN = main.cpp $(wildcard src/*.cpp)
OBJ_MAIN = $(SRC_MAIN:.cpp=.o)

SRC_MODBUS = modbus_server.cpp src/myLogger.cpp src/myModbus.cpp src/myRedis.cpp src/utils.cpp
OBJ_MODBUS = $(SRC_MODBUS:.cpp=.o)

DEPS = $(OBJ_MAIN:.o=.d) $(OBJ_MODBUS:.o=.d)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@ -MMD

all: $(OUT)

main: $(OBJ_MAIN)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS) $(MQTT_LIB)

modbus_server: $(OBJ_MODBUS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

debug: CXXFLAGS += -g
debug: $(OUT)

release: CXXFLAGS += -O3
release: $(OUT)

clean:
	rm -f $(OUT) $(OBJ_MAIN) $(OBJ_MODBUS) $(DEPS)

-include $(DEPS)

.PHONY: all debug release clean
