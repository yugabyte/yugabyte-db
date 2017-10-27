---
date: 2016-03-09T00:11:02+01:00
title: Test YugaByte Redis
weight: 7
---

After [creating a local cluster](/community-edition/quick-start/create-local-cluster/), follow the instructions below to test its Redis service.

[**redis-cli**](https://redis.io/topics/rediscli) is a command line interface to interact with a Redis server. For ease of use, the YugaByte DB package ships with the 4.0.1 version of redis-cli in its bin directory.


## Connect with redis-cli

<ul class="nav nav-tabs">
  <li class="active">
    <a data-toggle="tab" href="#docker">
      <i class="fa fa-docker" aria-hidden="true"></i>
      <b>Docker</b>
    </a>
  </li>
  <li >
    <a data-toggle="tab" href="#macos">
      <i class="fa fa-apple" aria-hidden="true"></i>
      <b>macOS</b>
    </a>
  </li>
  <li>
    <a data-toggle="tab" href="#linux">
      <i class="fa fa-linux" aria-hidden="true"></i>
      <b>Linux</b>
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="docker" class="tab-pane fade in active">
    {{% includeMarkdown "community-edition/quick-start/docker/test-yugabyte-redis.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade">
    {{% includeMarkdown "community-edition/quick-start/binary/macos-test-yugabyte-redis.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "community-edition/quick-start/binary/linux-test-yugabyte-redis.md" /%}}
  </div> 
</div>


## Simple key-value types

Insert a key and a value.

```
127.0.0.1:6379> set mykey somevalue
OK
```

Query the value by the key.

```
127.0.0.1:6379> get mykey
"somevalue"
```

Check if the key exists.

```
127.0.0.1:6379> exists mykey
(integer) 1
```


If the value is a number, it can be incremented.

```
127.0.0.1:6379> set counter 100
OK
127.0.0.1:6379> incr counter
(integer) 101
127.0.0.1:6379> incr counter
(integer) 102
127.0.0.1:6379> get counter
"102"
```


## Hash data types

You can create a Redis Hash data type as follows. This models the data for user id 1000 with the following attributes {username : antirez, birthyear : 1977, verified : 1}.

```
hmset user:1000 username antirez birthyear 1977 verified 1
```

You can retrieve specific attributes for user id 1000 as follows.

```
127.0.0.1:6379> hget user:1000 username
"antirez"
127.0.0.1:6379> hget user:1000 birthyear
"1977"
```

You can fetch multiple attributes with a single command as follows.

```
127.0.0.1:6379> hmget user:1000 username birthyear no-such-field
1) "antirez"
2) "1977"
3) (nil)
```

You can fetch all attributes by using the `hgetall` command.

```
127.0.0.1:6379> hgetall user:1000
1) "birthyear"
2) "1977"
3) "username"
4) "antirez"
5) "verified"
6) "1"
```

