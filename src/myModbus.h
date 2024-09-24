#ifndef MYMODBUS_H
#define MYMODBUS_H

#include <iomanip>
#include <iostream>

#include "nlohmann/json.hpp"
#include "spdlog/async.h"
#include "spdlog/spdlog.h"
#include <modbus/modbus.h>

using json = nlohmann::json;

extern std::unique_ptr<modbus_mapping_t, decltype(&modbus_mapping_free)> mb_mapping;

class MyModbusClient {
private:
    const std::string m_ip;
    int m_port;
    int m_slave_id;
    std::unique_ptr<modbus_t, decltype(&modbus_free)> ctx;

    void connect();

public:
    MyModbusClient(const std::string ip, int port, int slave_id);
    MyModbusClient(const MyModbusClient&) = delete;
    MyModbusClient& operator=(const MyModbusClient&) = delete;
    MyModbusClient(MyModbusClient&&) = default;
    MyModbusClient& operator=(MyModbusClient&&) = default;
    ~MyModbusClient() noexcept;

    std::vector<uint16_t> read_registers(int start_registers, int nb_registers);
};

class MyModbusServer {
private:
    const std::string m_ip;
    int m_port;
    std::unique_ptr<uint8_t[]> query;
    std::unique_ptr<modbus_t, decltype(&modbus_free)> ctx;
    int listen_sock, client_sock;
    unsigned int start_bits = 0;
    unsigned int start_input_bits = 0;
    unsigned int start_input_registers = 0;
    unsigned int start_registers = 0;
    unsigned int nb_bits = 100; // 线圈读写个数
    unsigned int nb_input_bits = 10000; // 离散量读个数
    unsigned int nb_input_registers = 10000; // 输入寄存器读个数
    unsigned int nb_registers = 10000; // 保持寄存器读写个数

    void init();
    void storeFloatToRegisters(uint16_t* tab_registers, std::size_t& index, float value);

public:
    MyModbusServer(const std::string ip, int port);
    MyModbusServer(const MyModbusServer&) = delete;
    MyModbusServer& operator=(const MyModbusServer&) = delete;
    MyModbusServer(MyModbusServer&&) = default;
    MyModbusServer& operator=(MyModbusServer&&) = default;
    ~MyModbusServer() noexcept;

    void run();
    void update(json& j, const std::string& name);
};

#endif // MYMODBUS_H