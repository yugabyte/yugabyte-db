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
Redis-cli or any Redis applications can access YugaByte database system. The following Redis commands are accepted.

Command | Description |
--------|-------------|
[`append`](append/) | Append data to end of string |
[`auth`](auth/) | Not supported. Accepted without processing |
[`config`](config/) | Not supported. Accepted without processing |
[`del`](del/) | Delete keys from database |
[`echo`](echo/) | Output messages |
[`exists`](exists/) | Check if the keys are present |
[`get`](get/) | Read string value |
[`getrange`](getrange/) | Read substring |
[`getset`](getset/) | Atomically read and write a string |
[`hdel`](hdel/) | Remove specified entries from a hash |
[`hexists`](hexists/) | Check if the subkeys are present in the hash |
[`hget`](hget/) | Read a field in hash |
[`hgetall`](hgetall/) | Read all the contents in a hash |
[`hkeys`](hkeys/) | Read all value-keys in a hash |
[`hlen`](hlen/) | Get the number of entries in a hash |
[`hmget`](hmget/) | Read values for the given keys in a hash |
[`hmset`](hmset/) | Write values for the given keys in a hash |
[`hset`](hset/) | Write one entry in a hash |
[`hstrlen`](hstrlen/) | Read the length of a specified entry in a hash |
[`hvals`](hvals/) | Read all values in a hash |
[`incr`](incr/) | Increment the value by one |
[`mget`](mget/) | Read multiple keys |
[`mset`](mset/) | Write multiple key values |
[`role`](role/) | Read role of a node |
[`sadd`](sadd/) | Add entries to a set |
[`scard`](scard/) | Read the number of entries in a set |
[`set`](set/) | Write or overwrite a string value |
[`setrange`](setrange/) | Write a subsection of a string |
[`sismember`](sismember/) | Check if the members are present in a set |
[`smembers`](smembers/) | Read all members of a set |
[`srem`](srem/) | Remove members from a set |
[`strlen`](strlen/) | Read the length of a string|
