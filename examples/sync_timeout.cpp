#include <string>
#include <vector>
#include <iostream>
#include <asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <redisclient/redissyncclient.h>

int main(int, char **)
{
    const unsigned short port = 6379;

    asio::io_context ioService;
    redisclient::RedisSyncClient redisClient(ioService);
    asio::error_code ec;

    redisClient.setConnectTimeout(std::chrono::seconds(3))
        .setCommandTimeout(std::chrono::seconds(3));
    redisClient.connect("127.0.0.1", std::to_string(port), ec);

    if (ec)
    {
        std::cerr << "Can't connect to redis: " << ec.message() << std::endl;
        return EXIT_FAILURE;
    }

    while(1)
    {
        redisclient::RedisValue result = redisClient.command("GET", {"key"}, ec);

        if (ec)
        {
            std::cerr << "Network error: " << ec.message() << "\n";
            break;
        }
        else if (result.isError())
        {
            std::cerr << "GET error: " << result.toString() << "\n";
            return EXIT_FAILURE;
        }
        else
        {
            std::cout << "GET: " << result.toString() << "\n";
        }

		std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return EXIT_FAILURE;
}
