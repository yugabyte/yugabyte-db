## yba xcluster delete

Delete a YugabyteDB Anywhere xCluster

### Synopsis

Delete a xCluster in YugabyteDB Anywhere between two universes

```
yba xcluster delete [flags]
```

### Examples

```
yba xcluster delete --uuid <uuid>
```

### Options

```
  -u, --uuid string    [Required] The uuid of the xcluster to be deleted.
      --force-delete   [Optional] Force delete the universe xcluster despite errors. (default false)
  -f, --force          [Optional] Bypass the prompt for non-interactive usage.
  -h, --help           help for delete
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

