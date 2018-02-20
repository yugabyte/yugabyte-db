---
title: Test YugaByte DB Redis API
weight: 140
---

After [creating a local cluster](/quick-start/create-local-cluster/), follow the instructions below to test YugaByte DB's Redis API.

[**redis-cli**](https://redis.io/topics/rediscli) is a command line interface to interact with a Redis server. For ease of use, YugaByte DB ships with the 4.0.1 version of redis-cli in its bin directory.

## 1. Connect with redis-cli

<ul class="nav nav-tabs nav-tabs-yb">
  <li class="active">
    <a href="#docker">
      <i class="icon-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="#kubernetes">
      <i class="fa fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
  <li>
    <a href="#macos">
      <i class="fa fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#linux">
      <i class="fa fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="docker" class="tab-pane fade in active">
    {{% includeMarkdown "/quick-start/docker/test-redis.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade">
    {{% includeMarkdown "/quick-start/kubernetes/test-redis.md" /%}}
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

```{.sql .copy .separator-gt}
127.0.0.1:6379> set mykey somevalue
```
```sh
OK
```

Query the value by the key.

```{.sql .copy .separator-gt}
127.0.0.1:6379> get mykey
```
```sh
"somevalue"
```

Check if the key exists.

```{.sql .copy .separator-gt}
127.0.0.1:6379> exists mykey
```
```sh
(integer) 1
```


If the value is a number, it can be incremented.

```{.sql .copy .separator-gt}
127.0.0.1:6379> set counter 100
```
```sh
OK
```
```{.sql .copy .separator-gt}
127.0.0.1:6379> incr counter
```
```sh
(integer) 101
```
```{.sql .copy .separator-gt}
127.0.0.1:6379> incr counter
```
```sh
(integer) 102
```
```{.sql .copy .separator-gt}
127.0.0.1:6379> get counter
```
```sh
"102"
```


## 3. Hash data types

You can create a Redis Hash data type as follows. This models the data for user id 1000 with the following attributes {username : john, birthyear : 1977, verified : 1}.

```{.sql .copy .separator-gt}
127.0.0.1:6379> hmset user:1000 username john birthyear 1977 verified 1
```
```sh
OK
```

You can retrieve specific attributes for user id 1000 as follows.

```{.sql .copy .separator-gt}
127.0.0.1:6379> hget user:1000 username
```
```sh
"john"
```
```{.sql .copy .separator-gt}
127.0.0.1:6379> hget user:1000 birthyear
```
```sh
"1977"
```

You can fetch multiple attributes with a single command as follows.

```{.sql .copy .separator-gt}
127.0.0.1:6379> hmget user:1000 username birthyear no-such-field
```
```sh
1) "john"
2) "1977"
3) (nil)
```

You can fetch all attributes by using the `hgetall` command.

```{.sql .copy .separator-gt}
127.0.0.1:6379> hgetall user:1000
```
```sh
1) "birthyear"
2) "1977"
3) "username"
4) "john"
5) "verified"
6) "1"
```

