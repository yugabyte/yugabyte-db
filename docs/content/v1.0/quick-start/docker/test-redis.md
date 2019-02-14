---
---

- Run redis-cli to connect to the service.

```sh
$ docker exec -it yb-tserver-n3 /home/yugabyte/bin/redis-cli
```

```
127.0.0.1:6379> 
```

- Run a Redis command to verify it is working.

```sql
127.0.0.1:6379> PING
```

```
"PONG"
```
