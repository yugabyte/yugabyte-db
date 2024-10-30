<!--
+++
private = true
+++
-->

To open the YSQL shell, run `ysqlsh`.

```sh
$ docker exec -it yugabyte /home/yugabyte/bin/ysqlsh --echo-queries
```

```output
ysqlsh (11.2-YB-{{<yb-version version="preview">}}-b0)
Type "help" for help.

yugabyte=#
```
