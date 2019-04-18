/*
 * Copyright (C) Alex Nekipelov (alex@nekipelov.net)
 * License: MIT
 */

#ifndef REDISASYNCCLIENT_REDISCLIENT_H
#define REDISASYNCCLIENT_REDISCLIENT_H

#include <asio.hpp>
#include <string>
#include <list>
#include <type_traits>
#include <functional>

#include "redisclient/impl/redisclientimpl.h"
#include "redisvalue.h"
#include "redisbuffer.h"

namespace redisclient {

class RedisClientImpl;

class RedisAsyncClient {
public:
    RedisAsyncClient(const RedisAsyncClient&) = delete;
    RedisAsyncClient& operator=(const RedisAsyncClient&) = delete;
    // Subscribe handle.
    struct Handle {
        size_t id;
        std::string channel;
    };

    typedef RedisClientImpl::State State;

    RedisAsyncClient(asio::io_context &ioService);
    ~RedisAsyncClient();

    // Connect to redis server
    void connect(
            const asio::ip::tcp::endpoint &endpoint,
            std::function<void(asio::error_code)> handler);

    // Return true if is connected to redis.
    bool isConnected() const;

    // Return connection state. See RedisClientImpl::State.
    State state() const;

    // disconnect from redis and clear command queue
    void disconnect();

    // Set custom error handler.
    void installErrorHandler(
            std::function<void(const std::string &)> handler);

    // Execute command on Redis server with the list of arguments.
    void command(
            const std::string &cmd, std::deque<RedisBuffer> args,
            std::function<void(RedisValue)> handler = dummyHandler);

    // Subscribe to channel. Handler msgHandler will be called
    // when someone publish message on channel. Call unsubscribe 
    // to stop the subscription.
    Handle subscribe(const std::string &channelName,
                                       std::function<void(std::vector<char> msg)> msgHandler,
                                       std::function<void(RedisValue)> handler = &dummyHandler);


    Handle psubscribe(const std::string &pattern,
                                        std::function<void(std::vector<char> msg)> msgHandler,
                                        std::function<void(RedisValue)> handler = &dummyHandler);

    // Unsubscribe
    void unsubscribe(const Handle &handle);
    void punsubscribe(const Handle &handle);

    // Subscribe to channel. Handler msgHandler will be called
    // when someone publish message on channel; it will be 
    // unsubscribed after call.
    void singleShotSubscribe(
            const std::string &channel,
            std::function<void(std::vector<char> msg)> msgHandler,
            std::function<void(RedisValue)> handler = &dummyHandler);

    void singleShotPSubscribe(
            const std::string &channel,
            std::function<void(std::vector<char> msg)> msgHandler,
            std::function<void(RedisValue)> handler = &dummyHandler);

    // Publish message on channel.
    void publish(
            const std::string &channel, const RedisBuffer &msg,
            std::function<void(RedisValue)> handler = &dummyHandler);

    static void dummyHandler(RedisValue) {}

protected:
    bool stateValid() const;

private:
    std::shared_ptr<RedisClientImpl> pimpl;
};

}

#endif // REDISASYNCCLIENT_REDISCLIENT_H
