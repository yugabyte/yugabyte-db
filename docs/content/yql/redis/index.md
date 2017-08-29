---
title: Redis
summary: Redis overview and commands.
---
<style>
table {
  float: left;
}
</style>

<h2>Introduction</h2>
Redis is an API that can be used to access YugaByte database system. YugaByte server provides an adapter for Redis client to connect, send request, and receive result. All data values are on-disk persistence. 

<h2>Data Structures</h2>
The following datatypes can be read and written via Redis service. All data are on-disk persistent in YugaByte system.<br>

DataType | Description |
--------|-------------|
string | Supported |
hash | Supported |
set | Supported |
sorted set | Not supported |
list | Not supported |

<h2>Commands</h2>
Redis-cli or any Redis applications can access Yugabyte database system. The following commands are accepted by Yugabyte server.

Command | Description |
--------|-------------|
[`append`](/yql/redis/append/) | Appending data to end of string |
[`auth`](/yql/redis/auth/) | Not supported. Accepted without processing |
[`config`](/yql/redis/config/) | Not supported. Accepted without processing |
[`del`](/yql/redis/del/) | Deleting keys from database |
[`echo`](/yql/redis/echo/) | Output messages |
[`exists`](/yql/redis/exists/) | Predicate for key existence |
[`get`](/yql/redis/get/) | Reading string value |
[`getrange`](/yql/redis/getrange/) | Reading substring |
[`getset`](/yql/redis/getset/) | Atomically reading and writing a string |
[`hdel`](/yql/redis/hdel/) | Removing specified entries from a hash |
[`hexists`](/yql/redis/hexists/) | Predicate for field existence in hash |
[`hget`](/yql/redis/hget/) | Reading a field in hash |
[`hgetall`](/yql/redis/hgetall/) | Reading hash content |
[`hkeys`](/yql/redis/hkeys/) | Reading all value-keys in a hash |
[`hlen`](/yql/redis/hlen/) | Reading number of entries in a hash |
[`hmget`](/yql/redis/hmget/) | Reading values of given keys in a hash |
[`hmset`](/yql/redis/hmset/) | Writing values of given keys in a hash |
[`hset`](/yql/redis/hset/) | Writing one entry in a hash |
[`hstrlen`](/yql/redis/hstrlen/) | Reading the length of a specified entry in a hash |
[`hvals`](/yql/redis/hvals/) | Reading all values in a hash |
[`incr`](/yql/redis/incr/) | Incrementing a number by one |
[`mget`](/yql/redis/mget/) | Reading multiple strings |
[`mset`](/yql/redis/mset/) | Writing multiple strings |
[`role`](/yql/redis/role/) | Reading role of a node |
[`sadd`](/yql/redis/sadd/) | Writing entries to a set |
[`scard`](/yql/redis/scard/) | Reading number of entries in a set |
[`set`](/yql/redis/set/) | Writing or rewriting a string value |
[`setrange`](/yql/redis/setrange/) | Writing a subsection of a string |
[`sismember`](/yql/redis/sismember/) | Predicate for existence of a member in a set |
[`smembers`](/yql/redis/smembers/) | Reading all members of a set |
[`srem`](/yql/redis/srem/) | Removing members from a set |
[`strlen`](/yql/redis/strlen/) | Reading the length of a string|
