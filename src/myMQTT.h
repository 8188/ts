#ifndef MYMQTT_H
#define MYMQTT_H

#include "spdlog/async.h"
#include "spdlog/spdlog.h"
#include <mqtt/async_client.h>

constexpr const auto TIMEOUT { std::chrono::seconds(5) };

class MyMQTT {
private:
    mqtt::async_client client;
    mqtt::connect_options connOpts;

    mqtt::connect_options buildConnectOptions(const std::string& username, const std::string& password,
        const std::string& caCerts, const std::string& certfile,
        const std::string& keyFile, const std::string& keyFilePassword) const;
    void disconnect();

public:
    MyMQTT(const std::string& address, const std::string& clientId,
        const std::string& username, const std::string& password,
        const std::string& caCerts, const std::string& certfile,
        const std::string& keyFile, const std::string& keyFilePassword);
    MyMQTT(const MyMQTT&) = delete;
    MyMQTT& operator=(const MyMQTT&) = delete;
    MyMQTT(MyMQTT&&) = default;
    MyMQTT& operator=(MyMQTT&&) = default;
    ~MyMQTT() noexcept;

    void connect();
    void publish(const std::string& topic, const std::string& payload, int qos, bool retained = false);
};

#endif // MYMQTT_H