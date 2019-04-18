/*
 * Copyright (C) Alex Nekipelov (alex@nekipelov.net)
 * License: MIT
 */

#ifndef REDISCLIENT_REDISCLIENTIMPL_CPP
#define REDISCLIENT_REDISCLIENTIMPL_CPP

#include <boost/asio/write.hpp>

#include <algorithm>

#include "redisclientimpl.h"

namespace
{

    static const char crlf[] = {'\r', '\n'};
    inline void bufferAppend(std::vector<char> &vec, const std::string &s);
    inline void bufferAppend(std::vector<char> &vec, const std::vector<char> &s);
    inline void bufferAppend(std::vector<char> &vec, const char *s);
    inline void bufferAppend(std::vector<char> &vec, char c);
    template<size_t size>
    inline void bufferAppend(std::vector<char> &vec, const char (&s)[size]);

    inline void bufferAppend(std::vector<char> &vec, const redisclient::RedisBuffer &buf)
    {
        if (auto pval = std::get_if<std::string>(&buf.data))
            bufferAppend(vec, *pval);
        else
            bufferAppend(vec, std::get<std::vector<char>>(buf.data));
    }

    inline void bufferAppend(std::vector<char> &vec, const std::string &s)
    {
        vec.insert(vec.end(), s.begin(), s.end());
    }

    inline void bufferAppend(std::vector<char> &vec, const std::vector<char> &s)
    {
        vec.insert(vec.end(), s.begin(), s.end());
    }

    inline void bufferAppend(std::vector<char> &vec, const char *s)
    {
        vec.insert(vec.end(), s, s + strlen(s));
    }

    inline void bufferAppend(std::vector<char> &vec, char c)
    {
        vec.resize(vec.size() + 1);
        vec[vec.size() - 1] = c;
    }

    template<size_t size>
    inline void bufferAppend(std::vector<char> &vec, const char (&s)[size])
    {
        vec.insert(vec.end(), s, s + size);
    }
}

namespace redisclient {

RedisClientImpl::RedisClientImpl(asio::io_context &ioService_)
    : ioService(ioService_), strand(ioService), socket(ioService),
    bufSize(0),subscribeSeq(0), state(State::Unconnected)
{
}

RedisClientImpl::~RedisClientImpl()
{
    close();
}

void RedisClientImpl::close() noexcept
{
    asio::error_code ignored_ec;

    msgHandlers.clear();
    decltype(handlers)().swap(handlers);

    socket.cancel(ignored_ec);
    socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
    socket.close(ignored_ec);

    state = State::Closed;
}

RedisClientImpl::State RedisClientImpl::getState() const
{
    return state;
}

void RedisClientImpl::processMessage()
{
    socket.async_read_some(asio::buffer(buf),
                           std::bind(&RedisClientImpl::asyncRead,
                                       shared_from_this(), std::placeholders::_1, std::placeholders::_2));
}

void RedisClientImpl::doProcessMessage(RedisValue v)
{
    if( state == State::Subscribed )
    {
        std::vector<RedisValue> result = v.toArray();
        auto resultSize = result.size();

        if( resultSize >= 3 )
        {
            const RedisValue &command   = result[0];
            const RedisValue &queueName = result[(resultSize == 3)?1:2];
            const RedisValue &value     = result[(resultSize == 3)?2:3];
            const RedisValue &pattern   = (resultSize == 4) ? result[1] : queueName;

            std::string cmd = command.toString();

            if( cmd == "message" || cmd == "pmessage" )
            {
                SingleShotHandlersMap::iterator it = singleShotMsgHandlers.find(pattern.toString());
                if( it != singleShotMsgHandlers.end() )
                {
                    strand.post(std::bind(it->second, value.toByteArray()));
                    singleShotMsgHandlers.erase(it);
                }

                std::pair<MsgHandlersMap::iterator, MsgHandlersMap::iterator> pair =
                        msgHandlers.equal_range(pattern.toString());
                for(MsgHandlersMap::iterator handlerIt = pair.first;
                    handlerIt != pair.second; ++handlerIt)
                {
                    strand.post(std::bind(handlerIt->second.second, value.toByteArray()));
                }
            }
            else if( handlers.empty() == false &&
                    (cmd == "subscribe" || cmd == "unsubscribe" ||
                    cmd == "psubscribe" || cmd == "punsubscribe")
                   )
            {
                handlers.front()(std::move(v));
                handlers.pop();
            }
            else
            {
                std::stringstream ss;

                ss << "[RedisClient] invalid command: "
                   << command.toString();

                errorHandler(ss.str());
                return;
            }
        }

        else
        {
            errorHandler("[RedisClient] Protocol error");
            return;
        }
    }
    else
    {
        if( handlers.empty() == false )
        {
            handlers.front()(std::move(v));
            handlers.pop();
        }
        else
        {
            std::stringstream ss;

            ss << "[RedisClient] unexpected message: "
               <<  v.inspect();

            errorHandler(ss.str());
            return;
        }
    }
}

void RedisClientImpl::asyncWrite(const asio::error_code &ec, size_t)
{
    dataWrited.clear();

    if( ec )
    {
        errorHandler(ec.message());
        return;
    }

    if( dataQueued.empty() == false )
    {
        std::vector<asio::const_buffer> buffers(dataQueued.size());

        for(size_t i = 0; i < dataQueued.size(); ++i)
        {
            buffers[i] = asio::buffer(dataQueued[i]);
        }

        std::swap(dataQueued, dataWrited);

        asio::async_write(socket, buffers,
                std::bind(&RedisClientImpl::asyncWrite, shared_from_this(),
                    std::placeholders::_1, std::placeholders::_2));
    }
}

void RedisClientImpl::handleAsyncConnect(const asio::error_code &ec,
            std::function<void(asio::error_code)> handler)
{
    if( !ec )
    {
        asio::error_code ec2; // Ignore errors in set_option
        socket.set_option(asio::ip::tcp::no_delay(true), ec2);
        state = State::Connected;
        handler(ec);
        processMessage();
    }
    else
    {
        state = State::Unconnected;
        handler(ec);
    }
}

std::vector<char> RedisClientImpl::makeCommand(const std::deque<RedisBuffer> &items)
{
    std::vector<char> result;

    bufferAppend(result, '*');
    bufferAppend(result, std::to_string(items.size()));
    bufferAppend<>(result, crlf);

    for(const auto &item: items)
    {
        bufferAppend(result, '$');
        bufferAppend(result, std::to_string(item.size()));
        bufferAppend<>(result, crlf);
        bufferAppend(result, item);
        bufferAppend<>(result, crlf);
    }

    return result;
}

void RedisClientImpl::run(std::chrono::steady_clock::duration timeout)
{
	// Restart the io_context, as it may have been left in the "stopped" state
	// by a previous operation.
	ioService.restart();

	// Block until the asynchronous operation has completed, or timed out. If
	// the pending asynchronous operation is a composed operation, the deadline
	// applies to the entire operation, rather than individual operations on
	// the socket.
	ioService.run_for(timeout);

	// If the asynchronous operation completed successfully then the io_context
	// would have been stopped due to running out of work. If it was not
	// stopped, then the io_context::run_for call must have timed out.
	if (!ioService.stopped())
	{
		// Close the socket to cancel the outstanding asynchronous operation.
		socket.close();

		// Run the io_context again until the operation completes.
		ioService.run();
	}
}

size_t RedisClientImpl::read(asio::mutable_buffer buffer,
	std::chrono::steady_clock::duration timeout, asio::error_code& ec)
{
	// Start the asynchronous operation. The lambda that is used as a callback
	// will update the error and n variables when the operation completes. The
	// blocking_udp_client.cpp example shows how you can use std::bind rather
	// than a lambda.
	std::size_t n = 0;
	socket.async_read_some(
		buffer, 
		[&](std::error_code result_error,
			std::size_t result_n)
		{
			ec = result_error;
			n = result_n;
		});

	// Run the operation until it completes, or until the timeout.
	run(timeout);
	return n;
}

void RedisClientImpl::write(const std::vector<char>& data,
	std::chrono::steady_clock::duration timeout, asio::error_code& ec)
{
	// Start the asynchronous operation itself. The lambda that is used as a
	// callback will update the error variable when the operation completes.
	// The blocking_udp_client.cpp example shows how you can use std::bind
	// rather than a lambda.
	std::error_code error;
	asio::async_write(socket, asio::buffer(data),
		[&](asio::error_code result_error,
			std::size_t /*result_n*/)
		{
			ec = result_error;
		});

	// Run the operation until it completes, or until the timeout.
	run(timeout);
}

RedisValue RedisClientImpl::doSyncCommand(const std::deque<RedisBuffer> &command,
        const std::chrono::milliseconds &timeout,
        asio::error_code &ec)
{
    std::vector<char> data = makeCommand(command);
	write(data, timeout, ec);

    if( ec )
    {
        return RedisValue();
    }

    return syncReadResponse(timeout, ec);
}

RedisValue RedisClientImpl::doSyncCommand(const std::deque<std::deque<RedisBuffer>> &commands,
        const std::chrono::milliseconds &timeout,
        asio::error_code &ec)
{
    for(const auto &command: commands)
    {
		auto data = makeCommand(command);
		write(data, timeout, ec);
		if (ec)
		{
			return RedisValue();
		}
    }

    std::vector<RedisValue> responses;

    for(size_t i = 0; i < commands.size(); ++i)
    {
        responses.push_back(syncReadResponse(timeout, ec));

        if (ec)
        {
            return RedisValue();
        }
    }

    return RedisValue(std::move(responses));
}

RedisValue RedisClientImpl::syncReadResponse(
        const std::chrono::milliseconds &timeout,
        asio::error_code &ec)
{
    for(;;)
    {
        if (bufSize == 0)
        {
            bufSize = read(asio::buffer(buf), timeout, ec);

            if (ec)
                return RedisValue();
        }

        for(size_t pos = 0; pos < bufSize;)
        {
            std::pair<size_t, RedisParser::ParseResult> result =
                redisParser.parse(buf.data() + pos, bufSize - pos);

            pos += result.first;

            ::memmove(buf.data(), buf.data() + pos, bufSize - pos);
            bufSize -= pos;

            if( result.second == RedisParser::Completed )
            {
                return redisParser.result();
            }
            else if( result.second == RedisParser::Incompleted )
            {
                continue;
            }
            else
            {
                errorHandler("[RedisClient] Parser error");
                return RedisValue();
            }
        }
    }
}

void RedisClientImpl::doAsyncCommand(std::vector<char> buff,
                                     std::function<void(RedisValue)> handler)
{
    handlers.push( std::move(handler) );
    dataQueued.push_back(std::move(buff));

    if( dataWrited.empty() )
    {
        // start transmit process
        asyncWrite(asio::error_code(), 0);
    }
}

void RedisClientImpl::asyncRead(const asio::error_code &ec, const size_t size)
{
    if( ec || size == 0 )
    {
        if (ec != asio::error::operation_aborted)
        {
            errorHandler(ec.message());
        }
        return;
    }

    for(size_t pos = 0; pos < size;)
    {
        std::pair<size_t, RedisParser::ParseResult> result = redisParser.parse(buf.data() + pos, size - pos);

        if( result.second == RedisParser::Completed )
        {
            doProcessMessage(redisParser.result());
        }
        else if( result.second == RedisParser::Incompleted )
        {
            processMessage();
            return;
        }
        else
        {
            errorHandler("[RedisClient] Parser error");
            return;
        }

        pos += result.first;
    }

    processMessage();
}

void RedisClientImpl::onRedisError(const RedisValue &v)
{
    errorHandler(v.toString());
}

void RedisClientImpl::defaulErrorHandler(const std::string &s)
{
    throw std::runtime_error(s);
}

size_t RedisClientImpl::subscribe(
    const std::string &command,
    const std::string &channel,
    std::function<void(std::vector<char> msg)> msgHandler,
    std::function<void(RedisValue)> handler)
{
    assert(state == State::Connected ||
           state == State::Subscribed);

    if (state == State::Connected || state == State::Subscribed)
    {
        std::deque<RedisBuffer> items{ command, channel };

        post(std::bind(&RedisClientImpl::doAsyncCommand, this, makeCommand(items), std::move(handler)));
        msgHandlers.insert(std::make_pair(channel, std::make_pair(subscribeSeq, std::move(msgHandler))));
        state = State::Subscribed;

        return subscribeSeq++;
    }
    else
    {
        std::stringstream ss;

        ss << "RedisClientImpl::subscribe called with invalid state "
            << to_string(state);

        errorHandler(ss.str());
        return 0;
    }
}

void RedisClientImpl::singleShotSubscribe(
    const std::string &command,
    const std::string &channel,
    std::function<void(std::vector<char> msg)> msgHandler,
    std::function<void(RedisValue)> handler)
{
    assert(state == State::Connected ||
           state == State::Subscribed);

    if (state == State::Connected ||
        state == State::Subscribed)
    {
        std::deque<RedisBuffer> items{ command, channel };

        post(std::bind(&RedisClientImpl::doAsyncCommand, this, makeCommand(items), std::move(handler)));
        singleShotMsgHandlers.insert(std::make_pair(channel, std::move(msgHandler)));
        state = State::Subscribed;
    }
    else
    {
        std::stringstream ss;

        ss << "RedisClientImpl::singleShotSubscribe called with invalid state "
            << to_string(state);

        errorHandler(ss.str());
    }
}

void RedisClientImpl::unsubscribe(const std::string &command,
                                  size_t handleId,
                                  const std::string &channel,
                                  std::function<void(RedisValue)> handler)
{
#ifdef DEBUG
    static int recursion = 0;
    assert(recursion++ == 0);
#endif

    assert(state == State::Connected ||
           state == State::Subscribed);

    if (state == State::Connected ||
        state == State::Subscribed)
    {
        // Remove subscribe-handler
        typedef RedisClientImpl::MsgHandlersMap::iterator iterator;
        std::pair<iterator, iterator> pair = msgHandlers.equal_range(channel);

        for (iterator it = pair.first; it != pair.second;)
        {
            if (it->second.first == handleId)
            {
                msgHandlers.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        std::deque<RedisBuffer> items{ command, channel };

        // Unsubscribe command for Redis
        post(std::bind(&RedisClientImpl::doAsyncCommand, this,
             makeCommand(items), handler));
    }
    else
    {
        std::stringstream ss;

        ss << "RedisClientImpl::unsubscribe called with invalid state "
            << to_string(state);

#ifdef DEBUG
        --recursion;
#endif
        errorHandler(ss.str());
        return;
    }

#ifdef DEBUG
    --recursion;
#endif
}

}

#endif // REDISCLIENT_REDISCLIENTIMPL_CPP
