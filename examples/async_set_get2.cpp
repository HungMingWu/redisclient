#include <string>
#include <iostream>
#include <functional>
#include <asio.hpp>

#include <redisclient/redisasyncclient.h>

static const std::string redisKey = "unique-redis-key-example";
static const std::string redisValue = "unique-redis-value";

void handleConnected(asio::io_context &ioService, redisclient::RedisAsyncClient &redis,
        asio::error_code ec)
{
    if( !ec )
    {
        redis.command("SET", {redisKey, redisValue}, [&](const redisclient::RedisValue &v) {
            std::cerr << "SET: " << v.toString() << std::endl;

            redis.command("GET", {redisKey}, [&](const redisclient::RedisValue &v) {
                std::cerr << "GET: " << v.toString() << std::endl;

                redis.command("DEL", {redisKey}, [&](const redisclient::RedisValue &) {
                    ioService.stop();
                });
            });
        });
    }
    else
    {
        std::cerr << "Can't connect to redis: " << ec.message() << std::endl;
    }
}

int main(int, char **)
{
    asio::ip::address address = asio::ip::address::from_string("127.0.0.1");
    const unsigned short port = 6379;
    asio::ip::tcp::endpoint endpoint(address, port);

    asio::io_context ioService;
    redisclient::RedisAsyncClient redis(ioService);

    redis.connect(endpoint,
            std::bind(&handleConnected, std::ref(ioService), std::ref(redis),
                std::placeholders::_1));

    ioService.run();

    return 0;
}
