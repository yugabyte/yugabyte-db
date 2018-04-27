- Run psql to connect to the service.

```{.sh .copy .separator-dollar}
$ kubectl exec -it yb-tserver-0 /home/yugabyte/bin/psql --host yb-tserver-0 --port 5433 
```
```sh
Database 'username' does not exist
psql (10.3, server 0.0.0)
Type "help" for help.

username=>
```
