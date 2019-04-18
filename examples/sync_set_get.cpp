#include <string>
#include <vector>
#include <iostream>
#include <asio.hpp>
#include <redisclient/redissyncclient.h>

int main(int, char **)
{
    const unsigned short port = 6379;

    asio::io_context ioService;
    redisclient::RedisSyncClient redis(ioService);
    asio::error_code ec;

    redis.connect("127.0.0.1", std::to_string(port), ec);

    if(ec)
    {
        std::cerr << "Can't connect to redis: " << ec.message() << std::endl;
        return EXIT_FAILURE;
    }

    redisclient::RedisValue result;

    result = redis.command("SET", {"key", "value"});

    if( result.isError() )
    {
        std::cerr << "SET error: " << result.toString() << "\n";
        return EXIT_FAILURE;
    }

    result = redis.command("GET", {"key"});

    if( result.isOk() )
    {
        std::cout << "GET: " << result.toString() << "\n";
        return EXIT_SUCCESS;
    }
    else
    {
        std::cerr << "GET error: " << result.toString() << "\n";
        return EXIT_FAILURE;
    }
}
