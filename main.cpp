#include "dotenv.h"
#include "src/myModbus.h"

#include "src/myLogger.h"
#include "src/Rotor.h"

constexpr const long long TASK_INTERVAL { 5000000 };
constexpr const int MQTT_SEND_PERIOD { 20 };
constexpr const bool PARAS_FROM_Redis { false };

class Task {
private:
    std::vector<Rotor> rotors;
    const std::vector<std::string> m_names;

public:
    Task(const std::vector<std::string>& names, const std::string& unit, const std::vector<Parameters>& paraList, const std::vector<int>& controlWords,
        std::shared_ptr<MyRedis> redisCli, std::shared_ptr<MyMQTT> MQTTCli,
        std::vector<std::unique_ptr<MyModbusClient>>&& modbusClis,
        std::shared_ptr<MyModbusServer> modbusServer)
        : m_names { names }
    {
        for (std::size_t i { 0 }; i < names.size(); ++i) {
            Rotor rotor(names[i], unit, paraList[i], controlWords[i], redisCli, MQTTCli, std::move(modbusClis[i]), modbusServer);
            rotors.emplace_back(std::move(rotor));
        }
    }

    void run(long long& count) {
        const std::size_t len { m_names.size() };
        
        while (true) {
            auto start = std::chrono::steady_clock::now();
            std::vector<std::future<void>> futures;

            for (std::size_t i { 0 }; i < len; ++i) {
                futures.emplace_back(std::async(std::launch::async, [this, i, &count]() {
                    rotors[i].run();
                    if (count % MQTT_SEND_PERIOD == 1) {
                        rotors[i].send_message();
                    }
                }));
            }

            for (auto& f : futures) {
                f.wait();
            }

            auto end = std::chrono::steady_clock::now();
            auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            std::cout << "Loop " << ++count << " time used: " << elapsed_time.count() << " microseconds\n";
            std::this_thread::sleep_for(std::chrono::microseconds(TASK_INTERVAL - elapsed_time.count()));
        }
    }
};

int main()
{
    init_logger();

    if (!fileExists(".env")) {
        spdlog::error("File .env does not exist!");
        return 1;
    }

    dotenv::init();
    const std::string MQTT_ADDRESS { std::getenv("MQTT_ADDRESS") };
    const std::string MQTT_USERNAME { std::getenv("MQTT_USERNAME") };
    const std::string MQTT_PASSWORD { std::getenv("MQTT_PASSWORD") };
    const std::string MQTT_CA_CERTS { std::getenv("MQTT_CA_CERTS") };
    const std::string MQTT_CERTFILE { std::getenv("MQTT_CERTFILE") };
    const std::string MQTT_KEYFILE { std::getenv("MQTT_KEYFILE") };
    const std::string MQTT_KEYFILE_PASSWORD { std::getenv("MQTT_KEYFILE_PASSWORD") };
    const std::string CLIENT_ID { generate_random_string_with_hyphens() };

    const std::string REDIS_IP { std::getenv("REDIS_IP") };
    const int REDIS_PORT = std::atoi(std::getenv("REDIS_PORT"));
    const int REDIS_DB = std::atoi(std::getenv("REDIS_DB"));
    const std::string REDIS_USER { std::getenv("REDIS_USER") };
    const std::string REDIS_PASSWORD { std::getenv("REDIS_PASSWORD") };
    const std::string REDIS_UNIX_SOCKET { std::getenv("REDIS_UNIX_SOCKET") };

    const std::string MODBUS_CLIENT_IP = std::getenv("MODBUS_CLIENT_IP");
    const int MODBUS_CLIENT_PORT = std::atoi(std::getenv("MODBUS_CLIENT_PORT"));

    const std::string MODBUS_SERVER_IP = std::getenv("MODBUS_SERVER_IP");
    const int MODBUS_SERVER_PORT = std::atoi(std::getenv("MODBUS_SERVER_PORT"));

    auto MQTTCli = std::make_shared<MyMQTT>(MQTT_ADDRESS, CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD,
        MQTT_CA_CERTS, MQTT_CERTFILE, MQTT_KEYFILE, MQTT_KEYFILE_PASSWORD);

    std::shared_ptr<MyRedis> redisCli;
    if (!REDIS_UNIX_SOCKET.empty()) {
        redisCli = std::make_shared<MyRedis>(REDIS_UNIX_SOCKET);
    } else {
        redisCli = std::make_shared<MyRedis>(REDIS_IP, REDIS_PORT, REDIS_DB, REDIS_USER, REDIS_PASSWORD);
    }

    const std::string unit1 { "1" };
    json j;

    if (PARAS_FROM_Redis) {
        j = redisCli->m_hgetall("TS" + unit1 + ":Mechanism:RotorParams");
    } else {
        std::ifstream file("parameters.json");
        if (!file) {
            return 1;
        }
        file >> j;
    }
    // std::cout << j.dump(4) << '\n';
    if (j.empty()) {
        spdlog::error("Empty parameters");
        return 1;
    }

    std::vector<std::string> keys;
    std::vector<Parameters> paraList;
    std::vector<std::unique_ptr<MyModbusClient>> modbusClis;
    std::vector<int> controlWords;

    for (json::iterator it = j.begin(); it != j.end(); ++it) {
        const std::string key = it.key();
        // std::cout << "key: " << key << ' ';
        keys.emplace_back(key);

        if (PARAS_FROM_Redis) {
            Parameters paras = loadParasFromRedis(j, key);
            paraList.emplace_back(paras);
        } else {
            Parameters paras = loadParasFromJson(j, key);
            // std::cout << paras << '\n';
            paraList.emplace_back(paras);
        }

        int slaveID = std::stoi(j[key]["slaveID"].get<std::string>());
        // libmodbus不支持shared_ptr
        auto modbusCli = std::make_unique<MyModbusClient>(MODBUS_CLIENT_IP, MODBUS_CLIENT_PORT, slaveID);
        modbusClis.emplace_back(std::move(modbusCli));

        const int controlWord = std::stoi(j[key]["controlWord"].get<std::string>());
        controlWords.emplace_back(controlWord);
    }

    auto modbusServer = std::make_shared<MyModbusServer>(MODBUS_SERVER_IP, MODBUS_SERVER_PORT);
    auto serverFuture = std::async(std::launch::async, [&]() { modbusServer.get()->run(); });

    Task task1 { keys, unit1, paraList, controlWords, redisCli, MQTTCli, std::move(modbusClis), modbusServer };
    long long count { 0 };
    auto clientFuture = std::async(std::launch::async, [&]() { task1.run(count); });

    return 0;
}