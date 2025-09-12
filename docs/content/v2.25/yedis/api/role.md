---
title: ROLE
linkTitle: ROLE
description: ROLE
menu:
  preview:
    parent: api-yedis
    weight: 2240
aliases:
  - /preview/api/redis/role
  - /preview/api/yedis/role
type: docs
---
YEDIS only has `master` role as far as Redis compatibility is concerned.

## Synopsis

**`ROLE`**

This command provides information of a Redis instance, such as its role, its state of replication, its followers, or its master. Roles are either "master", "follower", or "sentinel".

- Information of a master instance may include the following.
  * "master"
  * An integer that represents state of replication
  * An array of connected followers { IP address, IP port, State of replication }

- Information of a follower instance may include the following.
  * "follower"
  * Master IP address
  * Master IP port
  * Connection state that is either "disconnected", "connecting", "sync", or "connected"
  * An integer that represents state of replication

- Information of a sentinel instance may include the following.
  * "sentinel"
  * An array of master names.

## Return value

Returns an array of values.

## Examples

```sh
$ ROLE
```

```
1) "master"
2) 0
3) 1) 1) "127.0.0.1"
      2) "9200"
      3) "0"
   2) 1) "127.0.0.1"
      2) "9201"
      3) "0"
```

## See also

[`auth`](../auth/), [`config`](../config/)
