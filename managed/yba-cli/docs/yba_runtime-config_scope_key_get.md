## yba runtime-config scope key get

Get a YugabyteDB Anywhere runtime configuration scope key value

### Synopsis

Get a runtime configuration scope key value in YugabyteDB Anywhere. Run "yba runtime-config key-info list" to get the list of keys in a scope type.

```
yba runtime-config scope key get [flags]
```

### Examples

```
yba runtime-config scope key get --uuid <scope> --name <key-name>
```

### Options

```
  -u, --uuid string   [Required] The scope UUID of the key to be fetched.
  -n, --name string   [Required] The key name to be fetched.
  -h, --help          help for get
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

* [yba runtime-config scope key](yba_runtime-config_scope_key.md)	 - Manage YugabyteDB Anywhere runtime configuration scope keys

