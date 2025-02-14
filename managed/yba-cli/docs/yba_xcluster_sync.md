## yba xcluster sync

Reconcile a YugabyteDB Anywhere xcluster configuration with database

### Synopsis

If changes have been made to your xCluster configuration outside of YugabyteDB Anywhere (for example, using yb-admin), reconciling the xCluster configuration with the database updates the configuration in YugabyteDB Anywhere to match the changes.

```
yba xcluster sync [flags]
```

### Examples

```
yba xcluster sync --uuid <xcluster-uuid>
```

### Options

```
  -u, --uuid string   [Required] The uuid of the xcluster to sync.
  -f, --force         [Optional] Bypass the prompt for non-interactive usage.
  -h, --help          help for sync
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

