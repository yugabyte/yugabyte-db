## yba runtime-config scope

Get information about runtime configuration scope

### Synopsis

Get information about runtime configuration scope

```
yba runtime-config scope [flags]
```

### Options

```
  -h, --help   help for scope
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba runtime-config](yba_runtime-config.md)	 - Manage YugabyteDB Anywhere runtime configurations
* [yba runtime-config scope describe](yba_runtime-config_scope_describe.md)	 - Describe a YugabyteDB Anywhere runtime configuration scope
* [yba runtime-config scope key](yba_runtime-config_scope_key.md)	 - Manage YugabyteDB Anywhere runtime configuration scope keys
* [yba runtime-config scope list](yba_runtime-config_scope_list.md)	 - List runtime configuration scopes

