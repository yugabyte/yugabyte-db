## 1. Connect with redis-cli
- Initialize YugaByte DB's YEDIS API.

Setup the redis_keyspace keyspace and the .redis table so that this cluster becomes ready for redis clients. Detailed output for the setup_redis command is available in the [yb-ctl Reference](../../admin/yb-ctl/#setup-redis).
<div class='copy separator-dollar'>
```sh
$ ./bin/yb-ctl setup_redis
```
</div>

- Run redis-cli to connect to the service.

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ ./bin/redis-cli
```
</div>
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
