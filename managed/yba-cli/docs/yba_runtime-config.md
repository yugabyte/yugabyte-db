## yba runtime-config

Manage YugabyteDB Anywhere runtime configurations

### Synopsis

Manage YugabyteDB Anywhere runtime configurations

```
yba runtime-config [flags]
```

### Options

```
  -h, --help   help for runtime-config
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
* [yba runtime-config key-info](yba_runtime-config_key-info.md)	 - Get information about runtime configuration keys
* [yba runtime-config scope](yba_runtime-config_scope.md)	 - Get information about runtime configuration scope

