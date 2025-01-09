## yba xcluster list

List a YugabyteDB Anywhere xcluster between two universes

### Synopsis

List a xcluster in YugabyteDB Anywhere between two universes

```
yba xcluster list [flags]
```

### Examples

```
yba xcluster list --source-universe-name <source-universe-name> \
	  --target-universe-name <target-universe-name>
```

### Options

```
      --source-universe-name string   [Required] The name of the source universe for the xcluster.
      --target-universe-name string   [Required] The name of the target universe for the xcluster.
  -h, --help                          help for list
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

