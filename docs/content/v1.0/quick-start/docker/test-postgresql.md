---
---

-  Install the psql client inside docker

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ docker exec -it yb-tserver-n3 yum install postgresql -y
```
</div>

- Run psql to connect to the service.

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ docker exec -it yb-tserver-n3 /usr/bin/psql --host yb-tserver-n3 --port 5433
```
</div>

```sh
Database 'username' does not exist
psql (10.3, server 0.0.0)
Type "help" for help.

username=>
```
