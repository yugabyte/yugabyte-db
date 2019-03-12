---
---
Run psql to connect to the service.

```sh
$ kubectl exec -it yb-tserver-0 /home/yugabyte/bin/psql -- -U postgres -d postgres -h yb-tserver-0 -p 5433
```

```
psql (11.2)
Type "help" for help.

postgres=#
```
