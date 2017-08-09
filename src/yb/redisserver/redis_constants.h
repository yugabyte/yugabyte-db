// Copyright (c) YugaByte, Inc.

#ifndef YB_REDISSERVER_REDIS_CONSTANTS_H
#define YB_REDISSERVER_REDIS_CONSTANTS_H

static constexpr const char* const kRedisTableName = ".redis";
static constexpr const char* const kRedisKeyspaceName = "redis_keyspace";
static constexpr const char* const kRedisKeyColumnName = "key";
static constexpr uint16_t kRedisClusterSlots = 16384;

#endif  // YB_REDISSERVER_REDIS_CONSTANTS_H
