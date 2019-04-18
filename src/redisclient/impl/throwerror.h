#pragma once

#include <asio.hpp>

namespace redisclient
{

namespace detail
{

inline void throwError(const asio::error_code &ec)
{
    asio::system_error error(ec);
    throw error;
}

inline void throwIfError(const asio::error_code &ec)
{
    if (ec)
    {
        throwError(ec);
    }
}

}

}
