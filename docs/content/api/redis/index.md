---
title: Redis
summary: Redis overview and commands.
---
<style>
table {
  float: left;
}
</style>

## Introduction
YugaByte supports an automatically sharded, clustered & elastic Redis-as-a-Database in a Redis driver compatible manner. A Redis client can connect, send request, and receive result from YugaByte servers.

## Data Structures
The following datatypes can be read and written via Redis service. All data are on-disk persistent in YugaByte system.<br>

DataType | Development Status |
---------|-------------|
string | Supported |
hash | Supported |
set | Supported |
sorted set | Not yet supported |
list | Not yet supported |

## Commands
Redis-cli or any Redis applications can access Yugabyte database system. The following Redis commands are accepted.

Command | Description |
--------|-------------|
[`append`](append/) | Appending data to end of string |
[`auth`](auth/) | Not supported. Accepted without processing |
[`config`](config/) | Not supported. Accepted without processing |
[`del`](del/) | Deleting keys from database |
[`echo`](echo/) | Output messages |
[`exists`](exists/) | Predicate for key existence |
[`get`](get/) | Reading string value |
[`getrange`](getrange/) | Reading substring |
[`getset`](getset/) | Atomically reading and writing a string |
[`hdel`](hdel/) | Removing specified entries from a hash |
[`hexists`](hexists/) | Predicate for field existence in hash |
[`hget`](hget/) | Reading a field in hash |
[`hgetall`](hgetall/) | Reading hash content |
[`hkeys`](hkeys/) | Reading all value-keys in a hash |
[`hlen`](hlen/) | Reading number of entries in a hash |
[`hmget`](hmget/) | Reading values of given keys in a hash |
[`hmset`](hmset/) | Writing values of given keys in a hash |
[`hset`](hset/) | Writing one entry in a hash |
[`hstrlen`](hstrlen/) | Reading the length of a specified entry in a hash |
[`hvals`](hvals/) | Reading all values in a hash |
[`incr`](incr/) | Incrementing a number by one |
[`mget`](mget/) | Reading multiple strings |
[`mset`](mset/) | Writing multiple strings |
[`role`](role/) | Reading role of a node |
[`sadd`](sadd/) | Writing entries to a set |
[`scard`](scard/) | Reading number of entries in a set |
[`set`](set/) | Writing or rewriting a string value |
[`setrange`](setrange/) | Writing a subsection of a string |
[`sismember`](sismember/) | Predicate for existence of a member in a set |
[`smembers`](smembers/) | Reading all members of a set |
[`srem`](srem/) | Removing members from a set |
[`strlen`](strlen/) | Reading the length of a string|
