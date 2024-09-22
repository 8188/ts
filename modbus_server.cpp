#include "dotenv.h"

#include "src/myLogger.h"
#include "src/myModbus.h"
#include "src/myRedis.h"
#include "src/utils.h"

constexpr const int UPDATE_INTERVAL { 5000 };

int main()
{
    if (!fileExists(".env")) {
        spdlog::error("File .env does not exist!");
        return 1;
    }

    dotenv::init();

    const std::string REDIS_IP { std::getenv("REDIS_IP") };
    const int REDIS_PORT = std::atoi(std::getenv("REDIS_PORT"));
    const int REDIS_DB = std::atoi(std::getenv("REDIS_DB"));
    const std::string REDIS_USER { std::getenv("REDIS_USER") };
    const std::string REDIS_PASSWORD { std::getenv("REDIS_PASSWORD") };
    const std::string REDIS_UNIX_SOCKET { std::getenv("REDIS_UNIX_SOCKET") };

    std::shared_ptr<MyRedis> redisCli;
    if (!REDIS_UNIX_SOCKET.empty()) {
        redisCli = std::make_shared<MyRedis>(REDIS_UNIX_SOCKET);
    } else {
        redisCli = std::make_shared<MyRedis>(REDIS_IP, REDIS_PORT, REDIS_DB, REDIS_USER, REDIS_PASSWORD);
    }

    const std::string unit1 { "1" };

    auto modbusServer = std::make_unique<MyModbusServer>("127.0.0.1", 502, 1);

    auto serverFuture = std::async(std::launch::async, [&]() { modbusServer.get()->run(); });
    auto updateFuture = std::async(std::launch::async, [&]() {
        while (1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(UPDATE_INTERVAL));
            json j = redisCli->m_hgetall("TS" + unit1 + ":Mechanism:SendMessage");
            // std::cout << j.dump(4) << '\n';
            modbusServer.get()->update(j);
        }
    });

    serverFuture.wait();
    updateFuture.wait();

    return 0;
}