## yba provider list

List YugabyteDB Anywhere providers

### Synopsis

List YugabyteDB Anywhere providers

```
yba provider list [flags]
```

### Options

```
  -n, --name string   [OPTIONAL] Name of the provider.
  -c, --code string   [OPTIONAL] Code of the provider. Default lists all providers. Allowed values: aws, gcp, azu, onprem, kubernetes.
  -h, --help          help for list
```

### Options inherited from parent commands

```
  -a, --apiKey string      YugabyteDB Anywhere api key
      --config string      config file, default is $HOME/.yba-cli.yaml
      --debug              use debug mode, same as --logLevel debug
  -H, --host string        YugabyteDB Anywhere Host, default to http://localhost:9000
  -l, --logLevel string    select the desired log level format, default to info
      --no-color           disable colors in output , default to false
  -o, --output string      select the desired output format (table, json, pretty), default to table
      --timeout duration   wait command timeout,example: 5m, 1h. (default 168h0m0s)
      --wait               wait until the task is completed, otherwise it will exit immediately, default to true
```

### SEE ALSO

* [yba provider](yba_provider.md)	 - Manage YugabyteDB Anywhere providers

