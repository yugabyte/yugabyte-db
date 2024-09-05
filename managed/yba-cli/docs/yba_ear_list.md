## yba ear list

List YugabyteDB Anywhere Encryption at Rest Configurations

### Synopsis

List YugabyteDB Anywhere Encryption at Rest Configurations

```
yba ear list [flags]
```

### Options

```
  -n, --name string   [Optional] Name of the configuration.
  -c, --code string   [Optional] Code of the configuration, defaults to list all configs. Allowed values: aws, gcp, azu, hashicorp.
  -h, --help          help for list
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

* [yba ear](yba_ear.md)	 - Manage YugabyteDB Anywhere Encryption at Rest Configurations

