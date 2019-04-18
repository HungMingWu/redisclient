set(hdrs pipeline.h
         redisasyncclient.h
         redisbuffer.h
         redisparser.h
         redissyncclient.h
         redisvalue.h
         version.h
         impl/redisclientimpl.h
         impl/throwerror.h
)
set(srcs impl/pipeline.cpp
         impl/redisasyncclient.cpp
         impl/redisclientimpl.cpp
         impl/redisparser.cpp
         impl/redissyncclient.cpp
         impl/redisvalue.cpp
)

  RedisClientLibrary(RedisClient SOURCES ${srcs} HEADERS ${hdrs} WITH_INSTALL)
  target_compile_definitions(RedisClient
    PUBLIC
      REDIS_CLIENT_SEPARATED_COMPILATION
  )
  target_include_directories(RedisClient
    PUBLIC
      $<BUILD_INTERFACE:/redisclient/external/asio/asio/include>
  )
