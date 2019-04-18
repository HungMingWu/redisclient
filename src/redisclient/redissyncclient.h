/*
 * Copyright (C) Alex Nekipelov (alex@nekipelov.net)
 * License: MIT
 */

#ifndef REDISSYNCCLIENT_REDISCLIENT_H
#define REDISSYNCCLIENT_REDISCLIENT_H

#include <asio.hpp>

#include <chrono>
#include <string>
#include <list>
#include <functional>

#include "redisclient/impl/redisclientimpl.h"
#include "redisbuffer.h"
#include "redisvalue.h"

namespace redisclient {

class RedisClientImpl;
class Pipeline;

class RedisSyncClient {
public:
    RedisSyncClient(const RedisSyncClient&) = delete;
    RedisSyncClient& operator=(const RedisSyncClient&) = delete;
    typedef RedisClientImpl::State State;

    RedisSyncClient(asio::io_context &ioService);
    RedisSyncClient(RedisSyncClient &&other);
    ~RedisSyncClient();

    // Connect to redis server
    void connect(
            const asio::ip::tcp::endpoint &endpoint,
            asio::error_code &ec);

    // Connect to redis server
    void connect(
            const asio::ip::tcp::endpoint &endpoint);

#ifdef BOOST_ASIO_HAS_LOCAL_SOCKETS
    void connect(
            const asio::local::stream_protocol::endpoint &endpoint,
            asio::error_code &ec);

    void connect(
            const asio::local::stream_protocol::endpoint &endpoint);
#endif

    // Return true if is connected to redis.
    bool isConnected() const;

    // disconnect from redis
    void disconnect();

    // Set custom error handler.
    void installErrorHandler(
        std::function<void(const std::string &)> handler);

    // Execute command on Redis server with the list of arguments.
    RedisValue command(
            std::string cmd, std::deque<RedisBuffer> args);

    // Execute command on Redis server with the list of arguments.
    RedisValue command(
            std::string cmd, std::deque<RedisBuffer> args,
            asio::error_code &ec);

    // Create pipeline (see Pipeline)
    Pipeline pipelined();

    RedisValue pipelined(
            std::deque<std::deque<RedisBuffer>> commands,
            asio::error_code &ec);

    RedisValue pipelined(
            std::deque<std::deque<RedisBuffer>> commands);

    // Return connection state. See RedisClientImpl::State.
    State state() const;

    RedisSyncClient &setConnectTimeout(
            const std::chrono::milliseconds &timeout);
    RedisSyncClient &setCommandTimeout(
            const std::chrono::milliseconds &timeout);

    RedisSyncClient &setTcpNoDelay(bool enable);
    RedisSyncClient &setTcpKeepAlive(bool enable);

protected:
    bool stateValid() const;

private:
    std::shared_ptr<RedisClientImpl> pimpl;
    std::chrono::milliseconds connectTimeout;
    std::chrono::milliseconds commandTimeout;
    bool tcpNoDelay;
    bool tcpKeepAlive;
};

}

#endif // REDISSYNCCLIENT_REDISCLIENT_H
