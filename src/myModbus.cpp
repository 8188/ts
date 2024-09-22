#include "myModbus.h"

std::unique_ptr<modbus_mapping_t, decltype(&modbus_mapping_free)> mb_mapping { nullptr, &modbus_mapping_free };

void MyModbusClient::connect()
{
    try {
        ctx.reset(modbus_new_tcp(m_ip, m_port));
        if (ctx == nullptr) {
            throw std::runtime_error("Failed to allocate modbus context");
        }

        if (modbus_connect(ctx.get()) == -1) {
            throw std::runtime_error(std::string("Modbus connection failed: ") + modbus_strerror(errno));
        }

        spdlog::info("Connected to Modbus.");

        modbus_set_slave(ctx.get(), m_slave_id);
        modbus_set_response_timeout(ctx.get(), 0, 200000);
    } catch (const std::exception& e) {
        spdlog::warn("Exception from modbus connect: {}", e.what());
    }
}

MyModbusClient::MyModbusClient(const char* ip, int port, int slave_id)
    : m_ip { ip }
    , m_port { port }
    , m_slave_id { slave_id }
    , ctx { nullptr, &modbus_free }
{
    connect();
    int socket_fd = modbus_get_socket(ctx.get());
    if (socket_fd == -1) {
        spdlog::error("Modbus connection is not established.");
        std::terminate();
    }
}

MyModbusClient::~MyModbusClient() noexcept
{
    if (ctx) {
        modbus_close(ctx.get());
    }
}

std::vector<uint16_t> MyModbusClient::read_registers(int start_registers, int nb_registers)
{
    if (nb_registers <= 0) {
        return {};
    }

    std::vector<uint16_t> holding_registers(nb_registers);

    int blocks = nb_registers / MODBUS_MAX_READ_REGISTERS + (nb_registers % MODBUS_MAX_READ_REGISTERS != 0);

    try {
        for (int i { 0 }; i < blocks; ++i) {
            int addr = start_registers + i * MODBUS_MAX_READ_REGISTERS;
            int nb = (i == blocks - 1) ? nb_registers % MODBUS_MAX_READ_REGISTERS : MODBUS_MAX_READ_REGISTERS;
            int rc = modbus_read_registers(ctx.get(), addr, nb, &holding_registers[i * MODBUS_MAX_READ_REGISTERS]);
            if (rc == -1) {
                throw std::runtime_error(std::string("Read Holding Registers failed: ") + modbus_strerror(errno));
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("Exception from read_registers: {}", e.what());
        holding_registers.clear();
        std::this_thread::sleep_for(std::chrono::seconds(5));
        connect(); // Attempt to reconnect
    }
    return holding_registers;
}

void MyModbusServer::init()
{
    try {
        ctx.reset(modbus_new_tcp(m_ip, m_port));
        if (ctx == nullptr) {
            throw std::runtime_error("Failed to allocate modbus context");
        }

        modbus_set_slave(ctx.get(), m_slave_id);

        listen_sock = modbus_tcp_listen(ctx.get(), 1);
        if (listen_sock == -1) {
            spdlog::error("Unable to listen on Modbus TCP port: {}", modbus_strerror(errno));
        }

        client_sock = modbus_tcp_accept(ctx.get(), &listen_sock);
        if (client_sock == -1) {
            spdlog::warn("Failed to accept Modbus client: {}", modbus_strerror(errno));
        }

        query = (uint8_t*)malloc(MODBUS_TCP_MAX_ADU_LENGTH);
        if (query == NULL) {
            spdlog::error("Malloc Error: Out of memory!");
        }

        mb_mapping.reset(modbus_mapping_new_start_address(
            start_bits, nb_bits,
            start_input_bits, nb_input_bits,
            start_registers, nb_registers,
            start_input_registers, nb_input_registers));
        if (mb_mapping == nullptr) {
            spdlog::error("Unable to allocate the mapping: {}", modbus_strerror(errno));
        }

        for (std::size_t i { 0 }; i < nb_bits / 8; ++i) {
            mb_mapping->tab_bits[8 * i] = 0xAA;
        }

        for (std::size_t i { 0 }; i < nb_input_bits / 8; ++i) {
            mb_mapping->tab_input_bits[8 * i] = 0xAA;
        }

        for (std::size_t i { 0 }; i < nb_input_registers; i++) {
            mb_mapping->tab_input_registers[i] = i;
        }

        for (std::size_t i { 0 }; i < nb_registers; i++) {
            mb_mapping->tab_registers[i] = i;
        }
    } catch (const std::exception& e) {
        spdlog::warn("Exception from modbus server init: {}", e.what());
    }
}

void MyModbusServer::storeFloatToRegisters(uint16_t* tab_registers, std::size_t& index, float value)
{
    uint32_t valueBits;
    std::memcpy(&valueBits, &value, sizeof(value));
    tab_registers[index++] = valueBits & 0xFFFF; // Low 16 bits
    tab_registers[index++] = (valueBits >> 16) & 0xFFFF; // High 16 bits
}

MyModbusServer::MyModbusServer(const char* ip, int port, int slave_id)
    : m_ip { ip }
    , m_port { port }
    , m_slave_id { slave_id }
    , ctx { nullptr, &modbus_free }
{
    init();
    int socket_fd = modbus_get_socket(ctx.get());
    if (socket_fd == -1) {
        spdlog::error("Modbus server connection is not established.");
        std::terminate();
    }
}

MyModbusServer::~MyModbusServer() noexcept
{
    if (ctx) {
        modbus_close(ctx.get());
    }
}

void MyModbusServer::run()
{
    int rc;

    while (1) {
        rc = modbus_receive(ctx.get(), query);
        if (rc == -1) {
            spdlog::warn("Modbus client disconnected.");
            client_sock = modbus_tcp_accept(ctx.get(), &listen_sock);
            if (client_sock == -1) {
                spdlog::warn("Failed to accept Modbus client: {}", modbus_strerror(errno));
            }
        } else {
            std::cout << "Received Modbus request (length=" << rc << "): ";
            std::cout << std::hex;
            for (int i { 0 }; i < rc; ++i) {
                std::cout << std::setw(2) << std::setfill('0') << static_cast<int>(query[i]) << " ";
            }
            std::cout << std::dec << '\n';

            rc = modbus_reply(ctx.get(), query, rc, mb_mapping.get());
            if (rc == -1) {
                spdlog::error("Failed to process Modbus request: {}", modbus_strerror(errno));
            }
        }
    }
}

void MyModbusServer::update(json j)
{
    std::size_t index = 0;

    try {
        for (const auto& item : j.items()) {
            const auto& value = item.value();

            storeFloatToRegisters(mb_mapping->tab_registers, index, value["centerThermalStress"]);
            storeFloatToRegisters(mb_mapping->tab_registers, index, value["lifeRatio"]);
            storeFloatToRegisters(mb_mapping->tab_registers, index, value["overhaulLifeRatio"]);
            storeFloatToRegisters(mb_mapping->tab_registers, index, value["surfaceThermalStress"]);
            storeFloatToRegisters(mb_mapping->tab_registers, index, value["t0"]);
            storeFloatToRegisters(mb_mapping->tab_registers, index, value["thermalStress"]);
            storeFloatToRegisters(mb_mapping->tab_registers, index, value["thermalStressMargin"]);
            storeFloatToRegisters(mb_mapping->tab_registers, index, value["ts"]);

            const auto& temperatures = value["temperature"];
            for (const auto& temp : temperatures) {
                storeFloatToRegisters(mb_mapping->tab_registers, index, temp);
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Error occurred during updating tab_registers: {}", e.what());
    }
}
