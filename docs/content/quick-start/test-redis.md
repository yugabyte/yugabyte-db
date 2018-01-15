---
title: Test YugaByte DB Redis API
weight: 140
---

After [creating a local cluster](/quick-start/create-local-cluster/), follow the instructions below to test YugaByte's Redis service.

[**redis-cli**](https://redis.io/topics/rediscli) is a command line interface to interact with a Redis server. For ease of use, YugaByte DB ships with the 4.0.1 version of redis-cli in its bin directory.

## 1. Connect with redis-cli

<ul class="nav nav-tabs">
  <li class="active">
    <a data-toggle="tab" href="#docker">
      <i class="icon-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li >
    <a data-toggle="tab" href="#macos">
      <i class="fa fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a data-toggle="tab" href="#linux">
      <i class="fa fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="docker" class="tab-pane fade in active">
    {{% includeMarkdown "/quick-start/docker/test-redis.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade">
    {{% includeMarkdown "/quick-start/binary/test-redis.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "/quick-start/binary/test-redis.md" /%}}
  </div> 
</div>


## 2. Simple key-value types

Insert a key and a value.

```sh
127.0.0.1:6379> set mykey somevalue
OK
```

Query the value by the key.

```sh
127.0.0.1:6379> get mykey
"somevalue"
```

Check if the key exists.

```sh
127.0.0.1:6379> exists mykey
(integer) 1
```


If the value is a number, it can be incremented.

```sh
127.0.0.1:6379> set counter 100
OK
127.0.0.1:6379> incr counter
(integer) 101
127.0.0.1:6379> incr counter
(integer) 102
127.0.0.1:6379> get counter
"102"
```


## 3. Hash data types

You can create a Redis Hash data type as follows. This models the data for user id 1000 with the following attributes {username : john, birthyear : 1977, verified : 1}.

```sh
127.0.0.1:6379> hmset user:1000 username john birthyear 1977 verified 1
```

You can retrieve specific attributes for user id 1000 as follows.

```sh
127.0.0.1:6379> hget user:1000 username
"john"
127.0.0.1:6379> hget user:1000 birthyear
"1977"
```

You can fetch multiple attributes with a single command as follows.

```sh
127.0.0.1:6379> hmget user:1000 username birthyear no-such-field
1) "john"
2) "1977"
3) (nil)
```

You can fetch all attributes by using the `hgetall` command.

```sh
127.0.0.1:6379> hgetall user:1000
1) "birthyear"
2) "1977"
3) "username"
4) "john"
5) "verified"
6) "1"
```

