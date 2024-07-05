#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "dotenv.h"
#include "nlohmann/json.hpp"
#include "taskflow/taskflow.hpp"
#include <modbus/modbus.h>
#include <mqtt/async_client.h>
#include <sw/redis++/redis++.h>

using json = nlohmann::json;

constexpr const long long INTERVAL { 500000 };
constexpr const int QOS { 1 };
constexpr const auto TIMEOUT { std::chrono::seconds(5) };

struct TempPoint {
    double last;
    double cur;
};

struct TempZone {
    std::vector<double> X;
    std::vector<double> Y;
};

struct Parameters {
    const double density;
    const double radius;
    const double holeRadius;
    const double deltaR;
    const double scanCycle;
    const double surfaceFactor;
    const double centerFactor;
    const double freeFactor;
    const TempZone tcz; // Thermal Conductivity
    const TempZone shz; // Specific Heat
    const TempZone emz; // Elastic Modulus
    const TempZone prz; // Poisson's ratio
    const TempZone lecz; // Linear expansion coefficient
    const TempZone SN1, SN2, SN3; // 材料曲线插值
    const std::array<double, 2> sn; // SN曲线温度设定点
};

Parameters loadParameters(const json& j, const std::string& key)
{
    return {
        j[key]["density"].get<double>(),
        j[key]["radius"].get<double>(),
        j[key]["holeRadius"].get<double>(),
        j[key]["deltaR"].get<double>(),
        j[key]["scanCycle"].get<double>(),
        j[key]["surfaceFactor"].get<double>(),
        j[key]["centerFactor"].get<double>(),
        j[key]["freeFactor"].get<double>(),
        { j[key]["tcz"]["X"].get<std::vector<double>>(), j[key]["tcz"]["Y"].get<std::vector<double>>() },
        { j[key]["shz"]["X"].get<std::vector<double>>(), j[key]["shz"]["Y"].get<std::vector<double>>() },
        { j[key]["emz"]["X"].get<std::vector<double>>(), j[key]["emz"]["Y"].get<std::vector<double>>() },
        { j[key]["prz"]["X"].get<std::vector<double>>(), j[key]["prz"]["Y"].get<std::vector<double>>() },
        { j[key]["lecz"]["X"].get<std::vector<double>>(), j[key]["lecz"]["Y"].get<std::vector<double>>() },
        { j[key]["SN1"]["X"].get<std::vector<double>>(), j[key]["SN1"]["Y"].get<std::vector<double>>() },
        { j[key]["SN2"]["X"].get<std::vector<double>>(), j[key]["SN2"]["Y"].get<std::vector<double>>() },
        { j[key]["SN3"]["X"].get<std::vector<double>>(), j[key]["SN3"]["Y"].get<std::vector<double>>() },
        j[key]["sn"].get<std::array<double, 2>>()
    };
}

bool fileExists(const std::string& filename)
{
    std::ifstream file(filename);
    return file.good();
}

std::string generate_random_string_with_hyphens()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    const char* hex_chars = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            ss << "-";
        }
        int index = dis(gen);
        ss << hex_chars[index];
    }

    return ss.str();
}

class MyModbus {
private:
    const char* m_ip;
    int m_port;
    int m_slave_id;
    std::unique_ptr<modbus_t, decltype(&modbus_free)> ctx;

    void connect()
    {
        try {
            ctx.reset(modbus_new_tcp(m_ip, m_port));
            if (ctx == nullptr) {
                throw std::runtime_error("Failed to allocate modbus context");
            }

            if (modbus_connect(ctx.get()) == -1) {
                throw std::runtime_error(std::string("Modbus connection failed: ") + modbus_strerror(errno));
            }

            std::cout << "Connected to modbus." << std::endl;

            modbus_set_slave(ctx.get(), m_slave_id);
            modbus_set_response_timeout(ctx.get(), 0, 200000);
        } catch (const std::exception& e) {
            std::cerr << "Connection error: " << e.what() << std::endl;
        }
    }

public:
    MyModbus(const char* ip, int port, int slave_id)
        : m_ip{ ip }
        , m_port{ port }
        , m_slave_id{ slave_id }
        , ctx{ nullptr, &modbus_free }
    {
        connect();
        int socket_fd = modbus_get_socket(ctx.get());
        if (socket_fd == -1) {
            throw std::runtime_error("Modbus connection is not established.");
        }
    }

    ~MyModbus()
    {
        if (ctx) {
            modbus_close(ctx.get());
        }
    }

    std::vector<uint16_t> read_registers(int start_registers, int nb_registers)
    {
        if (nb_registers <= 0) {
            return {};
        }

        std::vector<uint16_t> holding_registers(nb_registers);

        int blocks = nb_registers / MODBUS_MAX_READ_REGISTERS + (nb_registers % MODBUS_MAX_READ_REGISTERS != 0);

        try {
            for (int i = 0; i < blocks; ++i) {
                int addr = start_registers + i * MODBUS_MAX_READ_REGISTERS;
                int nb = (i == blocks - 1) ? nb_registers % MODBUS_MAX_READ_REGISTERS : MODBUS_MAX_READ_REGISTERS;
                int rc = modbus_read_registers(ctx.get(), addr, nb, &holding_registers[i * MODBUS_MAX_READ_REGISTERS]);
                if (rc == -1) {
                    throw std::runtime_error(std::string("Read Holding Registers failed: ") + modbus_strerror(errno));
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            holding_registers.clear();
            std::this_thread::sleep_for(std::chrono::seconds(5));
            connect(); // 断线重连
        }
        return holding_registers;
    }
};

class MyRedis {
private:
    sw::redis::Redis m_redis;
    
    sw::redis::ConnectionOptions makeConnectionOptions(const std::string& ip, int port, int db, const std::string& user, const std::string& password)
    {
        sw::redis::ConnectionOptions opts;
        opts.host = ip;
        opts.port = port;
        opts.db = db;
        if (!user.empty()) {
            opts.user = user;
        }
        if (!password.empty()) {
            opts.password = password;
        }
        opts.socket_timeout = std::chrono::milliseconds(50);
        return opts;
    }

    sw::redis::ConnectionPoolOptions makePoolOptions()
    {
        sw::redis::ConnectionPoolOptions pool_opts;
        pool_opts.size = 3;
        pool_opts.wait_timeout = std::chrono::milliseconds(50);
        return pool_opts;
    }

public:
    MyRedis(const std::string& ip, int port, int db, const std::string& user, const std::string& password)
        : m_redis(makeConnectionOptions(ip, port, db, user, password), makePoolOptions())
    {
        m_redis.ping();
        std::cout << "Connected to Redis.\n";
    }

    MyRedis(const std::string& unixSocket)
        : m_redis(unixSocket)
    {
        m_redis.ping();
        std::cout << "Connected to Redis by unix socket.\n";
    }

    double m_hget(const std::string& key, const std::string& field)
    {
        double res;
        try {
            const auto optional_str = m_redis.hget(key, field);
            res = std::stod(optional_str.value_or("0"));
        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
        return res;
    }

    void m_hset(const std::string& hash, const std::string& key, const std::string& value)
    {
        try {
            m_redis.hset(hash, key, value);
        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    }
};

class MyMQTT {
private:
    mqtt::async_client client;
    mqtt::connect_options connOpts;

    mqtt::connect_options buildConnectOptions(const std::string& username, const std::string& password,
        const std::string& caCerts, const std::string& certfile,
        const std::string& keyFile, const std::string& keyFilePassword) const
    {
        // mqtt::connect_options_builder()对应mqtt:/ip:port, ::ws()对应ws:/ip:port
        auto connBuilder = mqtt::connect_options_builder()
                               .user_name(username)
                               .password(password)
                               .keep_alive_interval(std::chrono::seconds(45));

        if (!caCerts.empty()) {
            mqtt::ssl_options ssl;
            ssl.set_trust_store(caCerts);
            ssl.set_key_store(certfile);
            ssl.set_private_key(keyFile);
            ssl.set_private_key_password(keyFilePassword);

            connBuilder.ssl(ssl);
        }

        return connBuilder.finalize();
    }

    void disconnect()
    {
        if (client.is_connected()) {
            client.disconnect()->wait();
            std::cout << "Disconnected from MQTT broker.\n";
        }
    }

public:
    MyMQTT(const std::string& address, const std::string& clientId,
        const std::string& username, const std::string& password,
        const std::string& caCerts, const std::string& certfile,
        const std::string& keyFile, const std::string& keyFilePassword)
        : client(address, clientId)
        , connOpts { buildConnectOptions(username, password, caCerts, certfile, keyFile, keyFilePassword) }
    {
        connect();
        if (!client.is_connected()) {
            throw std::runtime_error("MQTT connection is not established.");
        }
    }

    ~MyMQTT()
    {
        disconnect();
    }

    void connect()
    {
        try {
            client.connect(connOpts)->wait_for(TIMEOUT); // 断线重连
            std::cout << "Connected to MQTT broker.\n";
        } catch (const mqtt::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

    void publish(const std::string& topic, const std::string& payload, int qos, bool retained = false)
    {
        auto msg = mqtt::make_message(topic, payload, qos, retained);
        try {
            bool ok = client.publish(msg)->wait_for(TIMEOUT);
            if (!ok) {
                std::cerr << "Error: Publishing message timed out." << std::endl;
            }
        } catch (const mqtt::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            connect();
        }
    }
};

class Rotor {
public:
    Rotor(const std::string& name, const std::string& unit, const Parameters& para, const int controlWord,
        std::shared_ptr<MyRedis> redis, std::shared_ptr<MyMQTT> MQTTCli, std::unique_ptr<MyModbus> modbusCli)
        : m_name { name }
        , m_unit { unit }
        , m_para { para }
        , m_controlWord { controlWord }
        , m_redis { redis }
        , m_MQTTCli { MQTTCli }
        , m_ModbusCli { std::move(modbusCli) }
        , lifeRatio { 0 }
        , overhaulLifeRatio { 0 }
        , surfaceTemp {}
        , centerTemp {}
        , field {}
    {
        init();
    }

    void run()
    {
        get_control_command();

        temp_field();
        thermal_stress();
        life();
        
        surfaceTemp.cur = get_surface_temp(0, 600);
    }

    void send_message()
    {
        double lr = m_redis->m_hget("TS" + m_unit + ":Mechanism:Rotor" + m_name, "life");
        double olr = m_redis->m_hget("TS" + m_unit + ":Mechanism:Rotor" + m_name, "overhaulLife");

        json j;
        j["lifeRatio"] = lr;
        j["overhaulLifeRatio"] = olr;
        if (lr > 0.75) {
            j["alert"] = "建议转子大修";
        } else if (olr > 0.06) {
            j["alert"] = "建议转子报废";
        }
        j["ts"] = surfaceTemp.cur;
        j["temperature"] = fieldmHR;
        j["t0"] = centerTemp.cur;
        j["centerThermalStress"] = centerThermalStress;
        j["surfaceThermalStress"] = surfaceThermalStress;
        j["thermalStress"] = thermalStress;
        j["thermalStressMargin"] = thermalStressMargin;

        const std::string jsonString = j.dump();
        m_MQTTCli->publish("TS" + m_unit + "/Rotor" + m_name, jsonString, QOS);
    }

private:
    const std::string m_name;
    const std::string m_unit;
    const Parameters& m_para;
    const int m_controlWord;

    double tc;
    double sh;
    double aveTemp;
    double surfaceThermalStress;
    double centerThermalStress;
    double thermalStress;
    double thermalStressMargin;
    double thermalStressMax; // 最大寿命消耗率对应的应力
    double lifeRatio;
    double overhaulLifeRatio;

    TempPoint surfaceTemp;
    TempPoint centerTemp;
    std::array<TempPoint, 20> field;

    std::shared_ptr<MyRedis> m_redis;
    std::shared_ptr<MyMQTT> m_MQTTCli;
    std::unique_ptr<MyModbus> m_ModbusCli;

    // 输出
    std::array<double, 10> fieldmHR;

    void get_control_command()
    {
        std::vector<uint16_t> registers;
        try {
            registers = m_ModbusCli.get()->read_registers(m_controlWord, 1);
        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }

        uint16_t value = registers[0];
        bool firstBit = value & 0x1;
        bool secondBit = value & 0x2;

        if (firstBit) {
            m_redis->m_hset("TS" + m_unit + ":Mechanism:Rotor" + m_name, "life", "0");
        }
        if (secondBit) {
            m_redis->m_hset("TS" + m_unit + ":Mechanism:Rotor" + m_name, "overhaulLife", "0");
        }
    }

    double get_surface_temp(double min, double max)
    {
        std::vector<uint16_t> registers;
        try {
            registers = m_ModbusCli.get()->read_registers(0, 10);
            for (size_t i { 0 }; i < registers.size(); ++i) {
                std::cout << registers[i] << " ";
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
        std::cout << '\n';
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(min, max);
        return dis(gen);
    }

    template <typename T>
    double interpolation(double temp, const T& data, int pointNum = 8) const
    {
        if (pointNum == 0 || pointNum > data.X.size()) {
            return 0.0;
        }

        if (temp < data.X[0]) {
            return data.Y[0];
        } else if (temp >= data.X[pointNum - 1]) {
            return data.Y[pointNum - 1];
        } else {
            for (size_t i = 1; i < pointNum; ++i) {
                if (temp < data.X[i]) {
                    double value = data.Y[i - 1] + (data.Y[i] - data.Y[i - 1]) * (temp - data.X[i - 1]) / (data.X[i] - data.X[i - 1]);
                    return value;
                }
            }
        }
        return 0;
    }

    // 计算T1, T2~T19, T20
    void cal_T()
    {
        for (std::size_t i { 0 }; i < 20; ++i) {
            tc = interpolation(field[i].last, m_para.tcz);
            sh = interpolation(field[i].last, m_para.shz);
            double ri = m_para.radius - m_para.deltaR * i;

            double term1 = 2 * tc * m_para.scanCycle;
            double term2, term3, term4;
            if (i == 0) {
                term2 = 2 * (ri - m_para.deltaR / 4) * surfaceTemp.last;
                term3 = 3 * (ri - m_para.deltaR / 2) * field[i].last;
                term4 = (ri - m_para.deltaR) * field[i + 1].last;
            } else if (i != 20) {
                term2 = ri * field[i - 1].last;
                term3 = (2 * ri - m_para.deltaR) * field[i].last;
                term4 = (ri - m_para.deltaR) * field[i + 1].last;
            } else {
                term2 = ri * field[i - 1].last;
                term3 = 3 * (ri - m_para.deltaR / 2) * field[i].last;
                term4 = 2 * (ri - 3 * m_para.deltaR / 4) * centerTemp.last;
            }
            double denominator = (2 * ri - m_para.deltaR) * m_para.density * sh * m_para.deltaR * m_para.deltaR * 1000;

            double T = term1 * (term2 - term3 + term4) / denominator;
            T += field[i].last;
            field[i].cur = T;
        }
        // 计算中心孔温度
        centerTemp.cur = (3 * field[19].cur - field[18].cur) / 2;
    }

    void cal_average_T()
    {
        double temp { 0 }, temp1 { 0 }, temp2 { 0 }, temp3 { 0 };
        double ri, denominator, numerator;

        for (std::size_t i { 0 }; i < 20; ++i) {
            ri = m_para.radius - m_para.deltaR * i;
            denominator = 2 * ri * m_para.deltaR - m_para.deltaR * m_para.deltaR;
            numerator = denominator * field[i].cur;
            temp += numerator;
            temp1 += denominator;
            temp2 += numerator;
            temp3 += denominator;
            if (i % 2 != 0) {
                fieldmHR[(i - 1) / 2] = temp2 / temp3;
                temp2 = 0;
                temp3 = 0;
            }
        }
        aveTemp = temp / temp1;
    }

    void update_T()
    {
        for (std::size_t i { 0 }; i < 20; ++i) {
            field[i].last = field[i].cur;
        }
        centerTemp.last = centerTemp.cur;
        surfaceTemp.last = surfaceTemp.cur;
    }

    void init()
    {
        lifeRatio = m_redis->m_hget("TS" + m_unit + ":Mechanism:Rotor" + m_name, "life");
        overhaulLifeRatio = m_redis->m_hget("TS" + m_unit + ":Mechanism:Rotor" + m_name, "overhaulLife");

        double flangeTemp { get_surface_temp(0, 600) };
        for (std::size_t i; i < 20; ++i) {
            field[i].last = flangeTemp;
        }
        surfaceTemp.last = flangeTemp;
        centerTemp.last = flangeTemp;
    }

    void temp_field()
    {
        cal_T();
        cal_average_T();
        update_T();
    }

    void thermal_stress()
    {
        double em { interpolation(aveTemp, m_para.emz) };
        double pr { interpolation(aveTemp, m_para.prz) };
        double lec { interpolation(aveTemp, m_para.lecz, m_para.lecz.X.size()) };

        surfaceThermalStress = m_para.surfaceFactor * em * lec * (aveTemp - surfaceTemp.cur) / 1000 / (1 - pr);
        centerThermalStress = m_para.centerFactor * em * lec * (aveTemp - centerTemp.cur) / 1000 / (1 - pr);
        thermalStress = std::max(fabs(surfaceThermalStress), fabs(centerThermalStress));
        thermalStressMargin = 100.0 * (1 - thermalStress / m_para.freeFactor);
    }

    void life(double K = 1.0 /*热应力集中系数*/)
    {
        // 最小寿命消耗率对应的应力
        double thermalStressMin;
        TempZone SNx;
        double life, lifeConsumptionRate;

        if (aveTemp < m_para.sn[0]) {
            SNx = m_para.SN1;
        } else if (aveTemp > m_para.sn[1]) {
            SNx = m_para.SN3;
        } else {
            SNx = m_para.SN2;
        }
        thermalStressMin = fabs(SNx.X[0]);

        if (fabs(thermalStress) >= thermalStressMin) {
            if (thermalStress > thermalStressMax) {
                thermalStressMax = thermalStress;
            }
        } else {
            if (thermalStressMax > thermalStressMin) {
                life = interpolation(K * thermalStressMax, SNx, SNx.X.size());
                lifeConsumptionRate = 1.0 / life;
                lifeRatio += lifeConsumptionRate;
                overhaulLifeRatio += lifeConsumptionRate;
                thermalStressMax = 0;

                m_redis->m_hset("TS" + m_unit + ":Mechanism:Rotor" + m_name, "life", std::to_string(lifeRatio));
                m_redis->m_hset("TS" + m_unit + ":Mechanism:Rotor" + m_name, "overhaulLife", std::to_string(overhaulLifeRatio));
            }
        }
        std::cout << lifeRatio << '\n';
    }
};

class Task {
private:
    std::vector<Rotor> rotors;
    const std::vector<std::string> m_names;

public:
    Task(const std::vector<std::string>& names, const std::string& unit, const std::vector<Parameters>& paraList, const std::vector<int>& controlWords,
        std::shared_ptr<MyRedis> redisCli, std::shared_ptr<MyMQTT> MQTTCli, std::vector<std::unique_ptr<MyModbus>>&& modbusClis)
        : m_names { names }
    {
        for (size_t i { 0 }; i < names.size(); ++i) {
            Rotor rotor(names[i], unit, paraList[i], controlWords[i], redisCli, MQTTCli, std::move(modbusClis[i]));
            rotors.emplace_back(std::move(rotor));
        }
    }

    tf::Taskflow flow(long long& count)
    {
        tf::Taskflow f("F");

        const size_t len { m_names.size() };
        std::vector<tf::Task> tasks(len);
        for (size_t i { 0 }; i < len; ++i) {
            // 使用&i会导致段错误
            tasks[i] = f.emplace([this, i, &count]() {
                            rotors[i].run();
                            if (count % 20 == 1) {
                                rotors[i].send_message();
                            }
                        }).name(m_names[i]);
        }
        return f;
    }
};

int main()
{
    if (!fileExists(".env")) {
        throw std::runtime_error("File .env does not exist!");
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

    const char* MODBUS_IP = std::getenv("MODBUS_IP");
    const int MODBUS_PORT = std::atoi(std::getenv("MODBUS_PORT"));

    auto MQTTCli = std::make_shared<MyMQTT>(MQTT_ADDRESS, CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD,
        MQTT_CA_CERTS, MQTT_CERTFILE, MQTT_KEYFILE, MQTT_KEYFILE_PASSWORD);

    std::shared_ptr<MyRedis> redisCli;
    if (!REDIS_UNIX_SOCKET.empty()) {
        redisCli = std::make_shared<MyRedis>(REDIS_UNIX_SOCKET);
    } else {
        redisCli = std::make_shared<MyRedis>(REDIS_IP, REDIS_PORT, REDIS_DB, REDIS_USER, REDIS_PASSWORD);
    }

    std::ifstream file("parameters.json");
    if (!file) {
        std::cerr << "Failed to open parameters.json\n";
        return 1;
    }

    json j;
    file >> j;

    json paras = j["Parameters"];
    std::vector<std::string> keys;
    std::vector<Parameters> paraList;
    std::vector<std::unique_ptr<MyModbus>> modbusClis;
    std::vector<int> controlWords;

    for (json::iterator it = paras.begin(); it != paras.end(); ++it) {
        const std::string key = it.key();
        keys.emplace_back(key);

        const Parameters para = loadParameters(paras, key);
        paraList.emplace_back(para);

        int modbusID = paras[key]["modbusID"];
        // libmodbus不支持shared_ptr
        auto modbusCli = std::make_unique<MyModbus>(MODBUS_IP, MODBUS_PORT, modbusID);
        modbusClis.emplace_back(std::move(modbusCli));

        int controlWord = paras[key]["controlWord"];
        controlWords.emplace_back(controlWord);
    }
    const std::string unit1 { "1" };

    // modbusClis移出当前作用域
    Task task1 { keys, unit1, paraList, controlWords, redisCli, MQTTCli, std::move(modbusClis) };

    tf::Executor executor;
    long long count { 0 };
    tf::Taskflow f { task1.flow(count) };

    while (1) {
        auto start = std::chrono::steady_clock::now();

        executor.run(f).wait();

        auto end = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "Loop " << ++count << " time used: " << elapsed_time.count() << " microseconds\n";
        std::this_thread::sleep_for(std::chrono::microseconds(INTERVAL - elapsed_time.count()));
    }

    return 0;
}