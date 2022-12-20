---
title: HDEL
linkTitle: HDEL
description: HDEL
menu:
  preview:
    parent: api-yedis
    weight: 2100
aliases:
  - /preview/api/redis/hdel
  - /preview/api/yedis/hdel
type: docs
---

## Synopsis

**`HDEL key field [field ...]`**

This command removes the given `fields` from the hash that is associated with the given `key`.

- If the given `key` does not exist, it is characterized as an empty hash, and 0 is returned for no elements are removed.
- If the given `key` is associated with non-hash data, an error is raised.

## Return value

Depends on the configuration parameter `emulate_redis_responses`.

- If `emulate_redis_responses` is `true`, returns the number of existing fields in the hash that were removed by this command.
- If `emulate_redis_responses` is `false`, returns OK.

## Examples

- `emulate_redis_responses` is `true`.

  ```sh
  $ HSET yugahash moon "Moon"
  ```

  ```
  1
  ```

  ```sh
  $ HDEL yugahash moon
  ```

  ```
  1
  ```

  ```sh
  $ HDEL yugahash moon
  ```

  ```
  0
  ```

- `emulate_redis_responses` is `false`.

  ```sh
  $ HSET yugahash moon "Moon"
  ```

  ```
  "OK"
  ```

  ```sh
  $ HDEL yugahash moon
  ```

  ```
  "OK"
  ```

  ```sh
  $ HDEL yugahash moon
  ```

  ```
  "OK"
  ```

## See also

[`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
