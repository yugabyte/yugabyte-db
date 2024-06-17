## yba storage-config azure create

Create an Azure YugabyteDB Anywhere storage configuration

### Synopsis

Create an Azure storage configuration in YugabyteDB Anywhere

```
yba storage-config azure create [flags]
```

### Options

```
      --backup-location string   [Required] The complete backup location including "https://<account-name>.blob.core.windows.net/<container-name>/<blob-name>".
      --sas-token string         AZ SAS Token. Provide the token within double quotes. Can also be set using environment variable AZURE_STORAGE_SAS_TOKEN.
  -h, --help                     help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the storage configuration for the operation. Required for create, delete, describe, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba storage-config azure](yba_storage-config_azure.md)	 - Manage a YugabyteDB Anywhere Azure storage configuration

