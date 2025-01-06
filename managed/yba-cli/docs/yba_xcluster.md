## yba xcluster

Manage YugabyteDB Anywhere xClusters

### Synopsis

Manage YugabyteDB Anywhere xClusters (Asynchronous Replication)

```
yba xcluster [flags]
```

### Options

```
  -h, --help   help for xcluster
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba](yba.md)	 - yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.
* [yba xcluster delete](yba_xcluster_delete.md)	 - Delete a YugabyteDB Anywhere xCluster
* [yba xcluster describe](yba_xcluster_describe.md)	 - Describe a YugabyteDB Anywhere xcluster between two universes
* [yba xcluster list](yba_xcluster_list.md)	 - List a YugabyteDB Anywhere xcluster between two universes
* [yba xcluster pause](yba_xcluster_pause.md)	 - Pause a YugabyteDB Anywhere xCluster
* [yba xcluster restart](yba_xcluster_restart.md)	 - Restart replication for databases in the YugabyteDB Anywhere sxCluster configuration
* [yba xcluster resume](yba_xcluster_resume.md)	 - Resume a YugabyteDB Anywhere xCluster
* [yba xcluster sync](yba_xcluster_sync.md)	 - Reconcile a YugabyteDB Anywhere xcluster configuration with database

