#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "dotenv.h"
#include "nlohmann/json.hpp"
#include "taskflow/taskflow.hpp"
#include <mqtt/async_client.h>
#include <sw/redis++/redis++.h>

using json = nlohmann::json;

constexpr const long long INTERVAL { 500000 };
constexpr const int QOS { 1 };
constexpr const auto TIMEOUT { std::chrono::seconds(10) };

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

double generateRandomFloat(double min, double max)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(min, max);
    return dis(gen);
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

class MyRedis {
private:
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
    sw::redis::Redis redis;

    MyRedis(const std::string& ip, int port, int db, const std::string& user, const std::string& password)
        : redis(makeConnectionOptions(ip, port, db, user, password), makePoolOptions())
    {
        std::cout << "Connected to Redis.\n";
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
    }

    ~MyMQTT()
    {
        disconnect();
    }

    void connect()
    {
        client.connect(connOpts)->wait();
        std::cout << "Connected to MQTT broker.\n";
    }

    void publish(const std::string& topic, const std::string& payload, int qos, bool retained = false)
    {
        auto msg = mqtt::make_message(topic, payload, qos, retained);
        bool ok = client.publish(msg)->wait_for(TIMEOUT);
        if (!ok) {
            std::cerr << "Error: Publishing message timed out." << std::endl;
        }
    }
};

class Rotor {
public:
    Rotor(const std::string& name, const std::string& unit, const Parameters& para,
        std::shared_ptr<MyRedis> redis, std::shared_ptr<MyMQTT> MQTTCli)
        : m_name { name }
        , m_unit { unit }
        , m_para { para }
        , m_redis { redis }
        , m_MQTTCli { MQTTCli }
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
        temp_field();
        thermal_stress();
        life();
        
        surfaceTemp.cur = generateRandomFloat(0, 600);
    }

    void send_message()
    {
        const auto optional_str = m_redis->redis.hget("TS" + m_unit + ":Mechanism:Rotor" + m_name, "life");
        double lr = std::stod(optional_str.value_or("0"));
        const auto optional_str1 = m_redis->redis.hget("TS" + m_unit + ":Mechanism:Rotor" + m_name, "overhaulLife");
        double olr = std::stod(optional_str1.value_or("0"));

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

    // 输出
    std::array<double, 10> fieldmHR;

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
        const auto optional_str = m_redis->redis.hget("TS" + m_unit + ":Mechanism:Rotor" + m_name, "life");
        lifeRatio = std::stod(optional_str.value_or("0"));
        const auto optional_str1 = m_redis->redis.hget("TS" + m_unit + ":Mechanism:Rotor" + m_name, "overhaulLife");
        overhaulLifeRatio = std::stod(optional_str1.value_or("0"));

        double flangeTemp { generateRandomFloat(0, 600) }; // 通讯获得
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

                m_redis->redis.hset("TS" + m_unit + ":Mechanism:Rotor" + m_name, "life", std::to_string(lifeRatio));
                m_redis->redis.hset("TS" + m_unit + ":Mechanism:Rotor" + m_name, "overhaulLife", std::to_string(overhaulLifeRatio));
            }
        }
        std::cout << lifeRatio << '\n';
    }
};

class Task {
private:
    Rotor rotorHP;
    Rotor rotorIP;

public:
    Task(const std::string& unit, const Parameters& para,
        std::shared_ptr<MyRedis> redisCli, std::shared_ptr<MyMQTT> MQTTCli)
        : rotorHP { "HP", unit, para, redisCli, MQTTCli }
        , rotorIP { "IP", unit, para, redisCli, MQTTCli }
    {
    }

    tf::Taskflow flow(long long& count)
    {
        tf::Taskflow f("F");

        tf::Task fA = f.emplace([&]() {
                           rotorHP.run();
                           if (count % 20 == 1) {
                               rotorHP.send_message();
                           }
                       }).name("HP_Rator");

        tf::Task fB = f.emplace([&]() {
                           rotorIP.run();
                           if (count % 20 == 1) {
                               rotorIP.send_message();
                           }
                       }).name("IP_Rator");
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

    auto MQTTCli = std::make_shared<MyMQTT>(MQTT_ADDRESS, CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD,
        MQTT_CA_CERTS, MQTT_CERTFILE, MQTT_KEYFILE, MQTT_KEYFILE_PASSWORD);
    MQTTCli->connect();

    auto redisCli = std::make_shared<MyRedis>(REDIS_IP, REDIS_PORT, REDIS_DB, REDIS_USER, REDIS_PASSWORD);

    const std::string unit1 { "1" };
    const Parameters HP {
        .density = 7864,
        .radius = 1.05 / 2,
        .holeRadius = 0,
        .deltaR = (1.05 / 2 - 0) / 20,
        .scanCycle = static_cast<double>(INTERVAL) / 1000,
        .surfaceFactor = 1.55,
        .centerFactor = 1.0,
        .freeFactor = 510.0,
        .tcz { .X = { 25.0, 100.0, 200.0, 300.0, 400.0, 500.0, 550.0, 600.0 }, .Y = { 37.8, 35.7, 34.5, 34.6, 35.4, 36.4, 36.8, 37.1 } },
        .shz { .X = { 25.0, 100.0, 200.0, 300.0, 400.0, 500.0, 550.0, 600.0 }, .Y = { 436.0, 440.0, 466.0, 516.0, 591.0, 691.0, 750.0, 815.0 } },
        .emz { .X = { 25.0, 100.0, 200.0, 300.0, 400.0, 500.0, 550.0, 600.0 }, .Y = { 211.3, 210.0, 205.0, 198.0, 188.0, 176.0, 169.0, 161.0 } },
        .prz { .X = { 25.0, 100.0, 200.0, 300.0, 400.0, 500.0, 550.0, 600.0 }, .Y = { 0.282, 0.28, 0.28, 0.29, 0.29, 0.31, 0.32, 0.32 } },
        .lecz { .X = { 100.0, 200.0, 300.0, 400.0, 500.0, 550.0, 600.0 }, .Y = { 11.9, 12.4, 12.8, 13.2, 13.5, 13.7, 13.9 } },
        .SN1 { .X = { 300.0, 330.0, 350.0, 360.0, 370.0, 380.0, 390.0, 400.0, 410.0, 420.0, 430.0, 440.0, 450.0, 460.0, 470.0, 480.0, 490.0, 500.0 }, .Y = { 2.8e8, 4.33e7, 1.37e7, 7.88e6, 4.61e6, 2.73e6, 1.64e6, 1.0e6, 6.18e5, 3.85e5, 2.43e5, 1.55e5, 9.98e4, 6.49e4, 4.26e4, 2.82e4, 1.88e4, 1.27e4 } },
        .SN2 { .X = { 300.0, 330.0, 350.0, 360.0, 370.0, 380.0, 390.0, 400.0, 410.0, 420.0, 430.0, 440.0, 450.0, 460.0, 470.0, 480.0, 490.0, 500.0 }, .Y = { 2.26e7, 3.84e6, 1.29e6, 7.63e5, 4.58e5, 2.79e5, 1.72e5, 1.08e5, 6.8e4, 4.35e4, 2.81e4, 1.83e4, 1.21e4, 8.01e3, 5.37e3, 3.63e3, 2.48e3, 1.7e3 } },
        .SN3 { .X = { 300.0, 330.0, 350.0, 360.0, 370.0, 380.0, 390.0, 400.0, 410.0, 420.0, 430.0, 440.0, 450.0, 460.0, 470.0, 480.0, 490.0, 500.0 }, .Y = { 2.26e7, 3.84e6, 1.29e6, 7.63e5, 4.58e5, 2.79e5, 1.72e5, 1.08e5, 6.8e4, 4.35e4, 2.81e4, 1.83e4, 1.21e4, 8.01e3, 5.37e3, 3.63e3, 2.48e3, 1.7e3 } },
        .sn { 250, 400 }
    };

    Task task1 { unit1, HP, redisCli, MQTTCli };

    tf::Executor executor;
    long long count { 0 };
    
    while (1) {
        auto start = std::chrono::steady_clock::now();

        executor.run(task1.flow(count)).wait();

        auto end = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "Loop " << ++count << " time used: " << elapsed_time.count() << " microseconds\n";
        std::this_thread::sleep_for(std::chrono::microseconds(INTERVAL - elapsed_time.count()));
    }

    return 0;
}