#include <string>
#include <iostream>
#include <functional>
#include <asio.hpp>
#include <chrono>
#include <thread>

#include <redisclient/redisasyncclient.h>

static const std::string redisKey = "unique-redis-key-example";

class Worker
{
public:
    Worker(asio::io_context &ioService)
        : ioService(ioService), redisClient(ioService), timer(ioService),
        timeout(std::chrono::seconds(3))
    {
    }

    void run();
    void stop();

protected:
    void onConnect(asio::error_code ec);
    void onGet(const redisclient::RedisValue &value);
    void get();

    void onTimeout(const asio::error_code &ec);

private:
    asio::io_context &ioService;
    redisclient::RedisAsyncClient redisClient;
    asio::steady_timer timer;
    std::chrono::seconds timeout;
};

void Worker::run()
{
    const char *address = "127.0.0.1";
    const int port = 6379;

    asio::ip::tcp::endpoint endpoint(asio::ip::address::from_string(address), port);

    timer.expires_from_now(timeout);
    timer.async_wait(std::bind(&Worker::onTimeout, this, std::placeholders::_1));

    redisClient.connect(endpoint, std::bind(&Worker::onConnect, this,
                std::placeholders::_1));
}

void Worker::onConnect(asio::error_code ec)
{
    if (ec)
    {
        if (ec != asio::error::operation_aborted)
        {
            timer.cancel();
            std::cerr << "Can't connect to redis: " << ec.message() << "\n";
        }
    }
    else
    {
        std::cerr << "connected\n";
        get();
    }
}

void Worker::onGet(const redisclient::RedisValue &value)
{
    timer.cancel();
    std::cerr << "GET " << value.toString() << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(1));

    get();
}

void Worker::get()
{
    timer.expires_from_now(timeout);
    timer.async_wait(std::bind(&Worker::onTimeout, this, std::placeholders::_1));

    redisClient.command("GET",  {redisKey},
            std::bind(&Worker::onGet, this, std::placeholders::_1));
}


void Worker::onTimeout(const asio::error_code &ec)
{
    if (!ec)
    {
        std::cerr << "timeout!\n";

        redisClient.disconnect();
        // try again
        run();
    }
}

int main(int, char **)
{
    asio::io_context ioService;
    Worker worker(ioService);

    worker.run();

    ioService.run();

    return 0;
}
