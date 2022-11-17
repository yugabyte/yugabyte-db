---
title: HSET
linkTitle: HSET
description: HSET
menu:
  preview:
    parent: api-yedis
    weight: 2180
aliases:
  - /preview/api/redis/hset
  - /preview/api/yedis/hset
type: docs
---

## Synopsis

**`HSET key field value`**

This command sets the data for the given `field` of the hash that is associated with the given `key` with the given `value`. If the `field` already exists in the hash, it is overwritten.

- If the given `key` does not exist, an associated hash is created, and the `field` and `value` are inserted.
- If the given `key` is not associated with a hash, an error is raised.

## Return value

Depends on the configuration parameter `emulate_redis_responses`.
- If `emulate_redis_responses` is true, returns 1 if a new field is inserted and 0 if an existing field is updated.
- If `emulate_redis_responses` is false, returns OK

## Examples

- `emulate_redis_responses` is `true`.

  ```sh
  $ HSET yugahash area1 "America"
  ```

  ```
  1
  ```

  ```sh
  $ HSET yugahash area1 "North America"
  ```

  ```
  0
  ```

  ```sh
  $ HGET yugahash area1
  ```

  ```
  "North America"
  ```

- `emulate_redis_responses` is `false`.

  ```sh
  $ HSET yugahash area1 "America"
  ```

  ```
  "OK"
  ```

  ```sh
  $ HSET yugahash area1 "North America"
  ```

  ```
  "OK"
  ```

  ```sh
  $ HGET yugahash area1
  ```

  ```
  "North America"
  ```

## See also

[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hincrby`](../hincrby/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
