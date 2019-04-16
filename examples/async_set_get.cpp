#include <string>
#include <iostream>
#include <functional>
#include <boost/asio/ip/address.hpp>
#include <asio.hpp>

#include <redisclient/redisasyncclient.h>

static const std::string redisKey = "unique-redis-key-example";
static const std::string redisValue = "unique-redis-value";

class Worker
{
public:
    Worker(asio::io_context &ioService, redisclient::RedisAsyncClient &redisClient)
        : ioService(ioService), redisClient(redisClient)
    {}

    void onConnect(asio::error_code ec);
    void onSet(const redisclient::RedisValue &value);
    void onGet(const redisclient::RedisValue &value);
    void stop();

private:
    asio::io_context &ioService;
    redisclient::RedisAsyncClient &redisClient;
};

void Worker::onConnect(asio::error_code ec)
{
    if(ec)
    {
        std::cerr << "Can't connect to redis: " << ec.message() << "\n";
    }
    else
    {
        redisClient.command("SET",  {redisKey, redisValue},
                            std::bind(&Worker::onSet, this, std::placeholders::_1));
    }
}

void Worker::onSet(const redisclient::RedisValue &value)
{
    std::cerr << "SET: " << value.toString() << std::endl;
    if( value.toString() == "OK" )
    {
        redisClient.command("GET",  {redisKey},
                            std::bind(&Worker::onGet, this, std::placeholders::_1));
    }
    else
    {
        std::cerr << "Invalid value from redis: " << value.toString() << std::endl;
    }
}

void Worker::onGet(const redisclient::RedisValue &value)
{
    std::cerr << "GET " << value.toString() << std::endl;
    if( value.toString() != redisValue )
    {
        std::cerr << "Invalid value from redis: " << value.toString() << std::endl;
    }

    redisClient.command("DEL", {redisKey},
                        std::bind(&asio::io_context::stop, std::ref(ioService)));
}


int main(int, char **)
{
    asio::ip::address address = asio::ip::address::from_string("127.0.0.1");
    const int port = 6379;
    asio::ip::tcp::endpoint endpoint(address, port);

    asio::io_context ioService;
    redisclient::RedisAsyncClient client(ioService);
    Worker worker(ioService, client);

    client.connect(endpoint, std::bind(&Worker::onConnect, &worker,
                std::placeholders::_1));

    ioService.run();

    return 0;
}
