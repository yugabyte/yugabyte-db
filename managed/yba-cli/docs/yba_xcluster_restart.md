## yba xcluster restart

Restart replication for databases in the YugabyteDB Anywhere sxCluster configuration

### Synopsis

Restart replication for databases in the YugabyteDB Anywhere xCluster configuration

```
yba xcluster restart [flags]
```

### Examples

```
yba xcluster restart --uuid <xcluster-uuid> --storage-config-name <storage-config-name>
```

### Options

```
  -u, --uuid string                  [Required] The uuid of the xcluster to restart.
      --storage-config-name string   [Required] Storage config to be used for taking the backup for replication. 
      --dry-run                      [Optional] Run the pre-checks without actually running the subtasks. (default false)
      --table-uuid string            [Optional] Comma separated list of source universe table uuids to restart. If not specified, all tables will be restarted.
      --parallelism int              [Optional] Number of concurrent commands to run on nodes over SSH via "yb_backup" script. (default 8)
      --force-delete                 [Optional] Force restart the universe xcluster despite errors. (default false)
  -f, --force                        [Optional] Bypass the prompt for non-interactive usage.
  -h, --help                         help for restart
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

* [yba xcluster](yba_xcluster.md)	 - Manage YugabyteDB Anywhere xClusters

