#ifndef MYREDIS_H
#define MYREDIS_H

#include "nlohmann/json.hpp"
#include "spdlog/async.h"
#include "spdlog/spdlog.h"
#include <sw/redis++/redis++.h>

using json = nlohmann::json;

class MyRedis {
private:
    sw::redis::Redis m_redis;

    sw::redis::ConnectionOptions makeConnectionOptions(const std::string& ip, int port, int db, const std::string& user, const std::string& password);
    sw::redis::ConnectionPoolOptions makePoolOptions();

public:
    MyRedis(const std::string& ip, int port, int db, const std::string& user, const std::string& password);
    MyRedis(const std::string& unixSocket);

    double m_hget(const std::string& key, const std::string& field);
    void m_hset(const std::string& hash, const std::string& key, const std::string& value);
    json m_hgetall(const std::string& key);
};

#endif // MYREDIS_H