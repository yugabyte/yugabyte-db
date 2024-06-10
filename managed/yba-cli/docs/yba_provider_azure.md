## yba provider azure

Manage a YugabyteDB Anywhere Azure provider

### Synopsis

Manage an Azure provider in YugabyteDB Anywhere

```
yba provider azure [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the provider for the action. Required for create, delete, describe, update.
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

* [yba provider](yba_provider.md)	 - Manage YugabyteDB Anywhere providers
* [yba provider azure create](yba_provider_azure_create.md)	 - Create an Azure YugabyteDB Anywhere provider
* [yba provider azure delete](yba_provider_azure_delete.md)	 - Delete an Azure YugabyteDB Anywhere provider
* [yba provider azure describe](yba_provider_azure_describe.md)	 - Describe an Azure YugabyteDB Anywhere provider
* [yba provider azure list](yba_provider_azure_list.md)	 - List Azure YugabyteDB Anywhere providers
* [yba provider azure update](yba_provider_azure_update.md)	 - Update an Azure YugabyteDB Anywhere provider

