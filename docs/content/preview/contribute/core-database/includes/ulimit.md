<!--
+++
private = true
+++
-->

{{< note title="Note" >}}

The build may fail with "too many open files".
In that case, increase the nofile limit in `/etc/security/limits.conf` as follows:

```sh
echo "* - nofile 1048576" | sudo tee -a /etc/security/limits.conf
```

Start a new shell session, and check the limit increase with `ulimit -n`.

{{< /note >}}
