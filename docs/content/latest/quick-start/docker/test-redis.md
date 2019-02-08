## 1. Connect with redis-cli
- Run redis-cli to connect to the service.

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ docker exec -it yb-tserver-n3 /home/yugabyte/bin/redis-cli
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
