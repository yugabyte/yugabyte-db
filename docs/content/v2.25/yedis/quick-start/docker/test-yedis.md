<!--
+++
private = true
block_indexing = true
+++
-->

* The `yb-docker-ctl` utility initializes the YEDIS API automatically.

* Run redis-cli to connect to the service.

```sh
$ docker exec -it yb-tserver-n1 /home/yugabyte/bin/redis-cli
```

```output
127.0.0.1:6379>
```

* Run a Redis command to verify it is working.

```sh
127.0.0.1:6379> PING
```

```output
"PONG"
```
