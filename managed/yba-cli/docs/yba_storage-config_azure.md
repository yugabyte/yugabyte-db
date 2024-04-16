## yba storage-config azure

Manage a YugabyteDB Anywhere Azure storage configuration

### Synopsis

Manage an Azure storage configuration in YugabyteDB Anywhere

```
yba storage-config azure [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the storage configuration for the operation. Required for create, delete, describe, update.
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

* [yba storage-config](yba_storage-config.md)	 - Manage YugabyteDB Anywhere storage configurations
* [yba storage-config azure create](yba_storage-config_azure_create.md)	 - Create an Azure YugabyteDB Anywhere storage configuration
* [yba storage-config azure delete](yba_storage-config_azure_delete.md)	 - Delete an Azure YugabyteDB Anywhere storage configuration
* [yba storage-config azure describe](yba_storage-config_azure_describe.md)	 - Describe an Azure YugabyteDB Anywhere storage configuration
* [yba storage-config azure list](yba_storage-config_azure_list.md)	 - List YugabyteDB Anywhere storage-configurations
* [yba storage-config azure update](yba_storage-config_azure_update.md)	 - Update an Azure YugabyteDB Anywhere storage configuration

