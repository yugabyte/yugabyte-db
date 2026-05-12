<!--
+++
private = true
block_indexing = true
+++
-->

To open the YSQL shell, run ysqlsh.

```sh
$ docker exec -it yugabyte /home/yugabyte/bin/ysqlsh --echo-queries
```

```output
ysqlsh (15.2-YB-{{<yb-version version="stable">}}-b0)
Type "help" for help.

yugabyte=#
```
