#include <string>
#include <vector>
#include <iostream>
#include <asio.hpp>
#include <redisclient/pipeline.h>
#include <redisclient/redissyncclient.h>

int main(int, char **)
{
    const unsigned short port = 6379;

    asio::io_context ioService;
    redisclient::RedisSyncClient redis(ioService);
    asio::error_code ec;

    redis.connect("127.0.0.1", std::to_string(port), ec);

    if (ec)
    {
        std::cerr << "Can't connect to redis: " << ec.message() << std::endl;
        return EXIT_FAILURE;
    }

    redisclient::Pipeline pipeline = redis.pipelined().command("SET", {"key", "value"})
        .command("INCR", {"counter"})
        .command("INCR", {"counter"})
        .command("DECR", {"counter"})
        .command("GET", {"counter"})
        .command("DEL", {"counter"})
        .command("DEL", {"key"});
    redisclient::RedisValue result = pipeline.finish();

    if (result.isOk())
    {
        for(const auto &i: result.toArray())
        {
            std::cout << "Result: " << i.inspect() << "\n";
        }

        return EXIT_SUCCESS;
    }
    else
    {
        std::cerr << "GET error: " << result.toString() << "\n";
        return EXIT_FAILURE;
    }
}
