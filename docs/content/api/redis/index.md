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
[`append`](/api/redis/append/) | Appending data to end of string |
[`auth`](/api/redis/auth/) | Not supported. Accepted without processing |
[`config`](/api/redis/config/) | Not supported. Accepted without processing |
[`del`](/api/redis/del/) | Deleting keys from database |
[`echo`](/api/redis/echo/) | Output messages |
[`exists`](/api/redis/exists/) | Predicate for key existence |
[`get`](/api/redis/get/) | Reading string value |
[`getrange`](/api/redis/getrange/) | Reading substring |
[`getset`](/api/redis/getset/) | Atomically reading and writing a string |
[`hdel`](/api/redis/hdel/) | Removing specified entries from a hash |
[`hexists`](/api/redis/hexists/) | Predicate for field existence in hash |
[`hget`](/api/redis/hget/) | Reading a field in hash |
[`hgetall`](/api/redis/hgetall/) | Reading hash content |
[`hkeys`](/api/redis/hkeys/) | Reading all value-keys in a hash |
[`hlen`](/api/redis/hlen/) | Reading number of entries in a hash |
[`hmget`](/api/redis/hmget/) | Reading values of given keys in a hash |
[`hmset`](/api/redis/hmset/) | Writing values of given keys in a hash |
[`hset`](/api/redis/hset/) | Writing one entry in a hash |
[`hstrlen`](/api/redis/hstrlen/) | Reading the length of a specified entry in a hash |
[`hvals`](/api/redis/hvals/) | Reading all values in a hash |
[`incr`](/api/redis/incr/) | Incrementing a number by one |
[`mget`](/api/redis/mget/) | Reading multiple strings |
[`mset`](/api/redis/mset/) | Writing multiple strings |
[`role`](/api/redis/role/) | Reading role of a node |
[`sadd`](/api/redis/sadd/) | Writing entries to a set |
[`scard`](/api/redis/scard/) | Reading number of entries in a set |
[`set`](/api/redis/set/) | Writing or rewriting a string value |
[`setrange`](/api/redis/setrange/) | Writing a subsection of a string |
[`sismember`](/api/redis/sismember/) | Predicate for existence of a member in a set |
[`smembers`](/api/redis/smembers/) | Reading all members of a set |
[`srem`](/api/redis/srem/) | Removing members from a set |
[`strlen`](/api/redis/strlen/) | Reading the length of a string|
