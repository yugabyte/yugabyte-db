## yba universe edit ysql

Edit YSQL settings for a YugabyteDB Anywhere Universe

### Synopsis

Edit YSQL settings for a YugabyteDB Anywhere Universe

```
yba universe edit ysql [flags]
```

### Examples

```
yba universe edit ysql --name <universe-name>
```

### Options

```
      --ysql string                         [Optional] YSQL endpoint. Allowed values: enable, disable.
      --ysql-auth string                    [Optional] YSQL authentication. Allowed values: enable, disable.
      --ysql-password string                [Optional] YSQL authentication password. Use single quotes ('') to provide values with special characters. Required when YSQL authentication is enabled
      --connection-pooling string           [Optional] Connection Pooling settings. Allowed values: enable, disable.
      --ysql-server-http-port int           [Optional] YSQL Server HTTP Port. (default 13000)
      --ysql-server-rpc-port int            [Optional] YSQL Server RPC Port. (default 5433)
      --internal-ysql-server-rpc-port int   [Optional] Internal YSQL Server RPC Port used when connection pooling is enabled. (default 6433)
  -h, --help                                help for ysql
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Required] The name of the universe to be edited.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe edit](yba_universe_edit.md)	 - Edit a YugabyteDB Anywhere universe

