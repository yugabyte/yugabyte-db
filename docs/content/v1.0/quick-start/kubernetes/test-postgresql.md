---
---

-  Install the psql client inside the container

```sh
$ kubectl exec -it yb-tserver-2 yum install postgresql
```

- Run psql to connect to the service.

```sh
$ kubectl exec -it yb-tserver-2 bash 
```

```sh
$ psql --host localhost --port 5433 
```

```
Database 'username' does not exist
psql (10.3, server 0.0.0)
Type "help" for help.

username=>
```
