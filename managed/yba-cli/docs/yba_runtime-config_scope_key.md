## yba runtime-config scope key

Manage YugabyteDB Anywhere runtime configuration scope keys

### Synopsis

Manage YugabyteDB Anywhere runtime configuration scope keys

```
yba runtime-config scope key [flags]
```

### Options

```
  -h, --help   help for key
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

* [yba runtime-config scope](yba_runtime-config_scope.md)	 - Get information about runtime configuration scope
* [yba runtime-config scope key delete](yba_runtime-config_scope_key_delete.md)	 - Delete a YugabyteDB Anywhere runtime configuration scope key value
* [yba runtime-config scope key get](yba_runtime-config_scope_key_get.md)	 - Get a YugabyteDB Anywhere runtime configuration scope key value
* [yba runtime-config scope key set](yba_runtime-config_scope_key_set.md)	 - Set a YugabyteDB Anywhere runtime configuration scope key value

