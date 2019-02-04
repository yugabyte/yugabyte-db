---
---

-  Install the psql client inside the container

```{.sh .copy .separator-dollar}
$ kubectl exec -it yb-tserver-2 yum install postgresql
```

- Run psql to connect to the service.

```{.sh .copy .separator-dollar}
$ kubectl exec -it yb-tserver-2 bash 
```

```{.sh .copy .separator-dollar}
$ psql --host localhost --port 5433 
```

```sh
Database 'username' does not exist
psql (10.3, server 0.0.0)
Type "help" for help.

username=>
```
