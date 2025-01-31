## yba universe node add

Add a node instance to YugabyteDB Anywhere universe

### Synopsis

Add a node instance to YugabyteDB Anywhere universe.
Add a previously removed (or not in-use) node to the cluster and balance data onto it.

```
yba universe node add [flags]
```

### Examples

```
yba universe node add --name <universe-name> --node-name <node-name>
```

### Options

```
  -h, --help   help for add
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Required] The name of the universe for the corresponding node operations.
      --node-name string   [Optional] The name of the universe node for the corresponding node operations. Required for add, reboot, release, remove, reprovision, start, stop.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe node](yba_universe_node.md)	 - Manage YugabyteDB Anywhere universe nodes

