#include "myMQTT.h"

mqtt::connect_options MyMQTT::buildConnectOptions(const std::string& username, const std::string& password,
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

void MyMQTT::disconnect()
{
    if (client.is_connected()) {
        client.disconnect()->wait();
        spdlog::info("Disconnected from MQTT broker.");
    }
}

MyMQTT::MyMQTT(const std::string& address, const std::string& clientId,
    const std::string& username, const std::string& password,
    const std::string& caCerts, const std::string& certfile,
    const std::string& keyFile, const std::string& keyFilePassword)
    : client(address, clientId)
    , connOpts { buildConnectOptions(username, password, caCerts, certfile, keyFile, keyFilePassword) }
{
    connect();
    if (!client.is_connected()) {
        spdlog::error("MQTT connection is not established.");
        std::terminate();
    }
}

MyMQTT::~MyMQTT() noexcept
{
    disconnect();
}

void MyMQTT::connect()
{
    try {
        client.connect(connOpts)->wait_for(TIMEOUT); // 断线重连
        spdlog::info("Connected to MQTT broker.");
    } catch (const mqtt::exception& e) {
        spdlog::warn("Exception from MQTT connect: {}", e.what());
    }
}

void MyMQTT::publish(const std::string& topic, const std::string& payload, int qos, bool retained)
{
    auto msg = mqtt::make_message(topic, payload, qos, retained);
    try {
        bool ok = client.publish(msg)->wait_for(TIMEOUT);
        if (!ok) {
            spdlog::warn("Publishing message timed out.");
        }
    } catch (const mqtt::exception& e) {
        spdlog::warn("Exception from publish: {}", e.what());
        connect();
    }
}
