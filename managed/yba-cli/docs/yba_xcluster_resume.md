## yba xcluster resume

Resume a YugabyteDB Anywhere xCluster

### Synopsis

Resume a xCluster in YugabyteDB Anywhere between two universes

```
yba xcluster resume [flags]
```

### Examples

```
yba xcluster resume --uuid <xcluster-uuid>
```

### Options

```
  -u, --uuid string   [Required] The uuid of the xcluster to resume.
  -h, --help          help for resume
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

