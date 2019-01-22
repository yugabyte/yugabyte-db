---
title: ROLE
linkTitle: ROLE
description: ROLE
menu:
  latest:
    parent: api-redis
    weight: 2240
aliases:
  - /latest/api/redis/role
  - /latest/api/yedis/role
---
YugaByte DB only has `master` role for the Redis API.

## Synopsis
<b>`ROLE`</b><br>
This command provides information of a Redis instance, such as its role, its state of replication, its slaves, or its master. Roles are either "master", "slave", or "sentinel".
<li>Information of a master instance may include the following.
  <ol>
  <li>"master"</li>
  <li>An integer that represents state of replication</li>
  <li>An array of connected slaves { IP address, IP port, State of replication }</li>
  </ol>
</li>

<li>Information of a slave instance may include the following.
  <ol>
  <li>"slave"</li>
  <li>Master IP address</li>
  <li>Master IP port</li>
  <li>Connection state that is either "disconnected", "connecting", "sync", or "connected"</li>
  <li>An integer that represents state of replication</li>
  </ol>
</li>

<li>Information of a sentinel instance may include the following.
  <ol>
  <li>"sentinel"</li>
  <li>An array of master names.</li>
  </ol>
</li>

## Return Value
Returns an array of values.

## Examples
```{.sh .copy .separator-dollar}
$ ROLE
```
```sh
1) "master"
2) 0
3) 1) 1) "127.0.0.1"
      2) "9200"
      3) "0"
   2) 1) "127.0.0.1"
      2) "9201"
      3) "0"
```

## See Also
[`auth`](../auth/), [`config`](../config/)
