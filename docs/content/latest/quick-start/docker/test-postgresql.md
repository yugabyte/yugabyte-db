- Run psql to connect to the service.

```{.sh .copy .separator-dollar}
$ docker exec -it yb-tserver-n3 /home/yugabyte/bin/psql --host yb-tserver-n3 --port 5433
```
```sh
Database 'username' does not exist
psql (10.3, server 0.0.0)
Type "help" for help.

username=>
```