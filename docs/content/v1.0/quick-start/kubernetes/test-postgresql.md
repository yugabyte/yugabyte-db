---
---

-  Install the psql client inside the container

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ kubectl exec -it yb-tserver-2 yum install postgresql
```
</div>

- Run psql to connect to the service.

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ kubectl exec -it yb-tserver-2 bash 
```
</div>
<div class='copy separator-dollar'>
```sh
$ psql --host localhost --port 5433 
```
</div>

```sh
Database 'username' does not exist
psql (10.3, server 0.0.0)
Type "help" for help.

username=>
```
