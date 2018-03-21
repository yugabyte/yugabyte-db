- Run redis-cli to connect to the service.

```{.sh .copy .separator-dollar}
$ kubectl exec -it yb-tserver-0 /home/yugabyte/bin/redis-cli
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
