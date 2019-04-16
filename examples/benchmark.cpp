#include <string>
#include <iostream>
#include <regex>
#include <memory>
#include <chrono>

#include <asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/format.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/program_options.hpp>

#include <redisclient/redisasyncclient.h>


struct Config
{
    std::string address;
    uint16_t port;
    bool connectionPerWorker;
    size_t workersCount;
};

// Fetch operations-per-second value from redis-server
class OpsFetcher : public std::enable_shared_from_this<OpsFetcher> {
public:
    OpsFetcher(asio::io_context &ioService,
            std::shared_ptr<redisclient::RedisAsyncClient> redisClient)
        : ioService(ioService), timer(ioService), redisClient(redisClient),
        timeout(std::chrono::seconds(1))
    {
    }

    void run()
    {
        resetTimeout();
    }

protected:
    void onTimeout(const asio::error_code &errorCode)
    {
        if( errorCode != asio::error::operation_aborted )
        {
            resetTimeout();
            redisClient->command("info", {"stats"},
                    std::bind(&OpsFetcher::printOPS, shared_from_this(), std::placeholders::_1));
        }
    }

    void resetTimeout()
    {
        timer.expires_from_now(timeout);
        timer.async_wait(std::bind(&OpsFetcher::onTimeout, shared_from_this(), std::placeholders::_1));
    }

    void printOPS(const redisclient::RedisValue &stats)
    {
        static const std::string key("instantaneous_ops_per_sec");
        static const std::regex expression("instantaneous_ops_per_sec:(\\d+)");

        std::smatch matches;
        std::string s = stats.toString();

        if( std::regex_search(s, matches, expression) ) {
            unsigned long ops = std::stoul(matches[1]);

            std::cout << "redis server ops: " << ops << std::endl;
        }
    }

private:
    asio::io_context &ioService;
    asio::steady_timer timer;
    std::shared_ptr<redisclient::RedisAsyncClient> redisClient;
    const std::chrono::seconds timeout;

};

class Worker : public std::enable_shared_from_this<Worker>
{
public:
    Worker(asio::io_context &ioService,
            std::shared_ptr<redisclient::RedisAsyncClient> redisClient)
        : ioService(ioService), redisClient(redisClient)
    {
    }

    ~Worker()
    {
    }

    void run()
    {
        work();
    }

    void work()
    {
        static int i = 0;
        std::string key = str(boost::format("key %1%") % ++i);
        auto self = shared_from_this();

        redisClient->command("SET", {key, key}, [key, self](const redisclient::RedisValue &) {
            self->redisClient->command("GET", {key}, [key, self](const redisclient::RedisValue &result) {
                assert(result.toString() == key);
                (void)result; // fix unused warning

                self->redisClient->command("DEL", {key}, [key, self](const redisclient::RedisValue &) {
                    self->work();
                });
            });
        });
    }

private:
    asio::io_context &ioService;
    std::shared_ptr<redisclient::RedisAsyncClient> redisClient;
};

class Benchmark
{
public:
    Benchmark(asio::io_context &ioService, const Config &config)
        : ioService(ioService), config(config)
    {
    }

    void run()
    {
        asio::ip::tcp::endpoint endpoint(
                asio::ip::address::from_string(config.address), config.port);

        if( config.connectionPerWorker )
        {
            for(size_t i = 0; i < config.workersCount; ++i)
            {
                auto client = std::make_shared<redisclient::RedisAsyncClient>(ioService);
                auto worker = std::make_shared<Worker>(ioService, client);
                std::function<void()> callback = std::bind(&Benchmark::runWorker, this, worker);

                client->connect(endpoint,
                        std::bind(&Benchmark::onConnect, this,
                            std::placeholders::_1, callback));
            }
            {
                auto client = std::make_shared<redisclient::RedisAsyncClient>(ioService);
                auto rpcFetcher = std::make_shared<OpsFetcher>(ioService, client);
                std::function<void()> callback = std::bind(&Benchmark::runRpcFetcher, this, rpcFetcher);

                client->connect(endpoint, std::bind(&Benchmark::onConnect, this,
                            std::placeholders::_1, callback));
            }
        }
        else
        {
            auto client = std::make_shared<redisclient::RedisAsyncClient>(ioService);

            client->connect(endpoint, std::bind(&Benchmark::onConnect, this,
                        std::placeholders::_1, [=]() {
                for(size_t i = 0; i < config.workersCount; ++i)
                {
                    auto worker = std::make_shared<Worker>(ioService, client);
                    runWorker(worker);
                }

                auto rpcFetcher = std::make_shared<OpsFetcher>(ioService, client);
                runRpcFetcher(rpcFetcher);
            }));
        }
    }

private:
    void runWorker(std::shared_ptr<Worker> worker)
    {
        worker->run();
    }

    void runRpcFetcher(std::shared_ptr<OpsFetcher> rpcFetcher)
    {
        rpcFetcher->run();
    }

    void onConnect(asio::error_code ec, std::function<void()> callback)
    {
        if(ec)
        {
            std::cerr << "Can't connect to redis " << config.address << ":" << config.port
                << ": " << ec.message() << "\n";
            ioService.stop();
        }
        else
        {
            callback();
        }
    }

private:
    asio::io_context &ioService;
    Config config;
};

int main(int argc, char **argv)
{
    namespace po = boost::program_options;

    Config config;

    po::options_description description("Options");
    description.add_options()
        ("help", "produce help message")
        ("address", po::value(&config.address)->default_value("127.0.0.1"),
             "redis server ip address")
        ("port", po::value(&config.port)->default_value(6379), "redis server port")
        ("workers", po::value(&config.workersCount)->default_value(50), "count of workers")
        ("connection-per-worker",  po::value(&config.connectionPerWorker)->default_value(false),
             "if true, create connection per worker")
    ;

    po::variables_map vm;

    try
    {
        po::store(po::parse_command_line(argc, argv, description), vm);
        po::notify(vm);
    }
    catch(const po::error &e)
    {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    if( vm.count("help") )
    {
        std::cout << description << "\n";
        return EXIT_SUCCESS;
    }

    asio::io_context ioService;
    Benchmark benchmark(ioService, config);

    benchmark.run();
    ioService.run();

    return 0;
}

