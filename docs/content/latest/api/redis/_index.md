---
title: Redis
linkTitle: Redis
description: Redis
summary: Redis overview and commands
aliases:
  - /api/redis
menu:
  latest:
    identifier: api-redis
    parent: api
    weight: 2000
---

## Introduction
YugaByte supports an automatically sharded, clustered & elastic Redis-as-a-Database in a Redis driver compatible manner. A Redis client can connect, send request, and receive result from YugaByte servers.

## Data Types
The following datatypes can be read and written via Redis service. All data are on-disk persistent in YugaByte system.<br>

DataType | Development Status |
---------|-------------|
string | Supported |
hash | Supported |
set | Supported |
sorted set | Supported |
list | Not yet supported |
time series | New datatype in YugaByte! |

## Commands
Redis-cli or any Redis applications can access YugaByte database system. The following Redis commands are accepted.

Command | Description |
--------|-------------|
[`APPEND`](append/) | Append data to end of string |
[`AUTH`](auth/) | Not supported. Accepted without processing |
[`CONFIG`](config/) | Not supported. Accepted without processing |
[`DEL`](del/) | Delete keys from a database |
[`ECHO`](echo/) | Output messages |
[`EXISTS`](exists/) | Check if the keys are present |
[`FLUSHALL`](flushall/) | Delete all keys from all databases |
[`FLUSHDB`](flushdb/) | Delete all keys from a database |
[`GET`](get/) | Read string value |
[`GETRANGE`](getrange/) | Read substring |
[`GETSET`](getset/) | Atomically read and write a string |
[`HDEL`](hdel/) | Remove specified entries from a hash |
[`HEXISTS`](hexists/) | Check if the subkeys are present in the hash |
[`HGET`](hget/) | Read a field in hash |
[`HGETALL`](hgetall/) | Read all the contents in a hash |
[`HKEYS`](hkeys/) | Read all value-keys in a hash |
[`HLEN`](hlen/) | Get the number of entries in a hash |
[`HMGET`](hmget/) | Read values for the given keys in a hash |
[`HMSET`](hmset/) | Write values for the given keys in a hash |
[`HSET`](hset/) | Write one entry in a hash |
[`HSTRLEN`](hstrlen/) | Read the length of a specified entry in a hash |
[`HVALS`](hvals/) | Read all values in a hash |
[`INCR`](incr/) | Increment the value by one |
[`MGET`](mget/) | Read multiple keys |
[`MSET`](mset/) | Write multiple key values |
[`ROLE`](role/) | Read role of a node |
[`SADD`](sadd/) | Add entries to a set |
[`SCARD`](scard/) | Read the number of entries in a set |
[`SET`](set/) | Write or overwrite a string value |
[`SETRANGE`](setrange/) | Write a subsection of a string |
[`SISMEMBER`](sismember/) | Check if the members are present in a set |
[`SMEMBERS`](smembers/) | Read all members of a set |
[`SREM`](srem/) | Remove members from a set |
[`STRLEN`](strlen/) | Read the length of a string |
[`TSADD`](tsadd/) | Add a time series entry |
[`TSCARD`](tscard/) | Retrieve the number of elements in the given time series |
[`TSGET`](tsget/) | Retrieve a time series entry |
[`TSLASTN`](tslastn/) | Retrieve the latest N time series entries for a given time series |
[`TSRANGEBYTIME`](tsrangebytime/) | Retrieve time series entries for a given time range |
[`TSREM`](tsrem/) | Delete a time series entry |
[`TSREVRANGEBYTIME`](tsrevrangebytime/) | Retrieve time series entries for a given time range ordered from newest to oldest |
[`ZADD`](zadd/) | Add a sorted set entry |
[`ZCARD`](zcard/) | Get cardinality of a sorted set |
[`ZRANGEBYSCORE`](zrangebyscore/) | Retrieve sorted set entries for a given score range |
[`ZREM`](zrem/) | Delete a sorted set entry |
[`ZREVRANGE`](zrevrange/) | Retrieve sorted set entries for given index range ordered from highest to lowest score |
