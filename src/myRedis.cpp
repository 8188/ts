#include "myRedis.h"

sw::redis::ConnectionOptions MyRedis::makeConnectionOptions(const std::string& ip, int port, int db, const std::string& user, const std::string& password)
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

sw::redis::ConnectionPoolOptions MyRedis::makePoolOptions()
{
    sw::redis::ConnectionPoolOptions pool_opts;
    pool_opts.size = 3;
    pool_opts.wait_timeout = std::chrono::milliseconds(50);
    return pool_opts;
}

MyRedis::MyRedis(const std::string& ip, int port, int db, const std::string& user, const std::string& password)
    : m_redis(makeConnectionOptions(ip, port, db, user, password), makePoolOptions())
{
    m_redis.ping();
    spdlog::info("Connected to Redis.");
}

MyRedis::MyRedis(const std::string& unixSocket)
    : m_redis(unixSocket)
{
    m_redis.ping();
    spdlog::info("Connected to Redis by unix socket.");
}

double MyRedis::m_hget(const std::string& key, const std::string& field)
{
    double res {};
    try {
        const auto optional_str = m_redis.hget(key, field);
        res = std::stod(optional_str.value_or("0"));
    } catch (const std::exception& e) {
        spdlog::warn("Exception from m_hget: {}", e.what());
    }
    return res;
}

void MyRedis::m_hset(const std::string& hash, const std::string& key, const std::string& value)
{
    try {
        m_redis.hset(hash, key, value);
    } catch (const std::exception& e) {
        spdlog::warn("Exception from m_hset: {}", e.what());
    }
}

json MyRedis::m_hgetall(const std::string& key)
{
    std::unordered_map<std::string, std::string> hash;
    json res;
    try {
        m_redis.hgetall(key, std::inserter(hash, hash.end()));
        for (const auto& [field, value] : hash) {
            res[field] = json::parse(value);
        }
    } catch (const std::exception& e) {
        spdlog::warn("Exception from hgetall: {}", e.what());
    }
    return res;
}
