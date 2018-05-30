- Initialize YugaByte DB's YEDIS API.

Setup the redis_keyspace keyspace and the .redis table so that this cluster becomes ready for redis clients. Detailed output for the setup_redis command is available in the [yb-ctl Reference](../../admin/yb-ctl/#setup-redis).

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl setup_redis
```

- Run redis-cli to connect to the service.

```{.sh .copy .separator-dollar}
$ ./bin/redis-cli
```
```sh
127.0.0.1:6379> 
```

- Run a Redis command to verify it is working.

```{.sh .copy .separator-gt}
127.0.0.1:6379> PING
```
```sh
"PONG"
```
