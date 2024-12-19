<!--
+++
private = true
+++
-->

To open the YSQL shell (ysqlsh), run the following.

```sh
$ kubectl --namespace yb-demo exec -it yb-tserver-0 -- sh -c "cd /home/yugabyte && ysqlsh -h yb-tserver-0 --echo-queries"
```

```output
ysqlsh (11.2-YB-{{<yb-version version="preview">}}-b0)
Type "help" for help.

yugabyte=#
```
