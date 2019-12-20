
To use the retail demo database, run the following command:

```sh
$ ./bin/yugabyted demo
```

To connect to the service, run `ysqlsh`.

```sh
$ docker exec -it yb-tserver-n1 /home/yugabyte/bin/ysqlsh -h yb-tserver-n1  --echo-queries
```

```
ysqlsh (11.2-YB-2.0.8.0-b0)
Type "help" for help.

yugabyte=#
```
