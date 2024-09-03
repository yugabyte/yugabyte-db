## yba ear azure

Manage a YugabyteDB Anywhere Azure encryption at rest (EAR) configuration

### Synopsis

Manage an Azure encryption at rest (EAR) configuration in YugabyteDB Anywhere

```
yba ear azure [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the configuration for the action. Required for create, delete, describe, update.
  -h, --help          help for azure
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
* [yba ear azure delete](yba_ear_azure_delete.md)	 - Delete a YugabyteDB Anywhere Azure encryption at rest configuration
* [yba ear azure describe](yba_ear_azure_describe.md)	 - Describe an Azure YugabyteDB Anywhere Encryption In Transit (EAR) configuration
* [yba ear azure list](yba_ear_azure_list.md)	 - List Azure YugabyteDB Anywhere Encryption In Transit (EAR) configurations

