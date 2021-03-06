/*
 * Copyright (C) Alex Nekipelov (alex@nekipelov.net)
 * License: MIT
 */


#ifndef REDISSYNCCLIENT_REDISBUFFER_H
#define REDISSYNCCLIENT_REDISBUFFER_H

#include <string>
#include <vector>
#include <variant>

namespace redisclient {

struct RedisBuffer
{
    RedisBuffer() = default;
    inline RedisBuffer(const char *ptr, size_t dataSize);
    inline RedisBuffer(const char *s);
    inline RedisBuffer(std::string s);
    inline RedisBuffer(std::vector<char> buf);

    inline size_t size() const;

    std::variant<std::string,std::vector<char>> data;
};


RedisBuffer::RedisBuffer(const char *ptr, size_t dataSize)
    : data(std::vector<char>(ptr, ptr + dataSize))
{
}

RedisBuffer::RedisBuffer(const char *s)
    : data(std::string(s))
{
}

RedisBuffer::RedisBuffer(std::string s)
    : data(std::move(s))
{
}

RedisBuffer::RedisBuffer(std::vector<char> buf)
    : data(std::move(buf))
{
}

size_t RedisBuffer::size() const
{
    if (auto pval = std::get_if<std::string>(&data))
        return pval->size();
    else
        return std::get<std::vector<char>>(data).size();
}

}

#endif //REDISSYNCCLIENT_REDISBUFFER_H

