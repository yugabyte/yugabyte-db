## yba universe edit ycql

Edit YCQL settings for a YugabyteDB Anywhere Universe

### Synopsis

Edit YCQL settings for a YugabyteDB Anywhere Universe

```
yba universe edit ycql [flags]
```

### Examples

```
yba universe edit ycql --name <universe-name> --ycql-password <ycql-password>
```

### Options

```
      --ycql string                 [Optional] YCQL endpoint. Allowed values: enable, disable.
      --ycql-auth string            [Optional] YCQL authentication. Allowed values: enable, disable.
      --ycql-password string        [Optional] YCQL authentication password. Use single quotes ('') to provide values with special characters. Required when YCQL authentication is being enabled or disabled
      --ycql-server-http-port int   [Optional] YCQL Server HTTP Port. (default 13000)
      --ycql-server-rpc-port int    [Optional] YCQL Server RPC Port. (default 5433)
  -h, --help                        help for ycql
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Required] The name of the universe to be edited.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe edit](yba_universe_edit.md)	 - Edit a YugabyteDB Anywhere universe

