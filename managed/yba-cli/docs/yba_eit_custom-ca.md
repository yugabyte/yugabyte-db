## yba eit custom-ca

Manage a YugabyteDB Anywhere Custom CA encryption in transit (EIT) certificate configuration

### Synopsis

Manage a Custom CA encryption in transit (EIT) certificate configuration in YugabyteDB Anywhere

```
yba eit custom-ca [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the configuration for the action. Required for create, delete, describe, update.
  -h, --help          help for custom-ca
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

* [yba eit](yba_eit.md)	 - Manage YugabyteDB Anywhere Encryption In Transit (EIT) configurations
* [yba eit custom-ca delete](yba_eit_custom-ca_delete.md)	 - Delete a YugabyteDB Anywhere Custom CA encryption in transit configuration
* [yba eit custom-ca describe](yba_eit_custom-ca_describe.md)	 - Describe a Custom CA YugabyteDB Anywhere Encryption In Transit (EIT) configuration
* [yba eit custom-ca list](yba_eit_custom-ca_list.md)	 - List Custom CA YugabyteDB Anywhere Encryption In Transit (EIT) certificate configurations

