/*
 * Copyright (C) Alex Nekipelov (alex@nekipelov.net)
 * License: MIT
 */

#ifndef REDISCLIENT_REDISCLIENTIMPL_H
#define REDISCLIENT_REDISCLIENTIMPL_H

#include <asio.hpp>
#include <boost/asio/generic/stream_protocol.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/strand.hpp>

#include <chrono>
#include <array>
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <functional>
#include <memory>

#include "redisclient/redisparser.h"
#include "redisclient/redisbuffer.h"

namespace redisclient {

class RedisClientImpl : public std::enable_shared_from_this<RedisClientImpl> {
public:
    enum class State {
        Unconnected,
        Connecting,
        Connected,
        Subscribed,
        Closed
    };

    RedisClientImpl(asio::io_context &ioService);
    ~RedisClientImpl();

    void handleAsyncConnect(
            const asio::error_code &ec,
            std::function<void(asio::error_code)> handler);

    size_t subscribe(const std::string &command,
        const std::string &channel,
        std::function<void(std::vector<char> msg)> msgHandler,
        std::function<void(RedisValue)> handler);

    void singleShotSubscribe(const std::string &command,
        const std::string &channel,
        std::function<void(std::vector<char> msg)> msgHandler,
        std::function<void(RedisValue)> handler);

    void unsubscribe(const std::string &command,
        size_t handle_id, const std::string &channel,
        std::function<void(RedisValue)> handler);

    void close() noexcept;

    State getState() const;

    static std::vector<char> makeCommand(const std::deque<RedisBuffer> &items);

    RedisValue doSyncCommand(const std::deque<RedisBuffer> &command,
        const std::chrono::milliseconds &timeout,
        asio::error_code &ec);
    RedisValue doSyncCommand(const std::deque<std::deque<RedisBuffer>> &commands,
        const std::chrono::milliseconds &timeout,
        asio::error_code &ec);
    RedisValue syncReadResponse(
            const std::chrono::milliseconds &timeout,
            asio::error_code &ec);

    void doAsyncCommand(
            std::vector<char> buff,
            std::function<void(RedisValue)> handler);

    void sendNextCommand();
    void processMessage();
    void doProcessMessage(RedisValue v);
    void asyncWrite(const asio::error_code &ec, const size_t);
    void asyncRead(const asio::error_code &ec, const size_t);

    void onRedisError(const RedisValue &);
    static void defaulErrorHandler(const std::string &s);

    template<typename Handler>
    inline void post(const Handler &handler);

    asio::io_context &ioService;
    asio::io_context::strand strand;
    asio::generic::stream_protocol::socket socket;
    RedisParser redisParser;
    std::array<char, 4096> buf;
    size_t bufSize; // only for sync
    size_t subscribeSeq;

    typedef std::pair<size_t, std::function<void(const std::vector<char> &buf)> > MsgHandlerType;
    typedef std::function<void(const std::vector<char> &buf)> SingleShotHandlerType;

    typedef std::multimap<std::string, MsgHandlerType> MsgHandlersMap;
    typedef std::multimap<std::string, SingleShotHandlerType> SingleShotHandlersMap;

    std::queue<std::function<void(RedisValue)> > handlers;
    std::deque<std::vector<char>> dataWrited;
    std::deque<std::vector<char>> dataQueued;
    MsgHandlersMap msgHandlers;
    SingleShotHandlersMap singleShotMsgHandlers;

    std::function<void(const std::string &)> errorHandler;
    State state;
};

template<typename Handler>
inline void RedisClientImpl::post(const Handler &handler)
{
    strand.post(handler);
}

inline std::string to_string(RedisClientImpl::State state)
{
    switch(state)
    {
        case RedisClientImpl::State::Unconnected:
            return "Unconnected";
            break;
        case RedisClientImpl::State::Connecting:
            return "Connecting";
            break;
        case RedisClientImpl::State::Connected:
            return "Connected";
            break;
        case RedisClientImpl::State::Subscribed:
            return "Subscribed";
            break;
        case RedisClientImpl::State::Closed:
            return "Closed";
            break;
    }

    return "Invalid";
}
}


#endif // REDISCLIENT_REDISCLIENTIMPL_H
