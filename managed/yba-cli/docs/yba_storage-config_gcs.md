## yba storage-config gcs

Manage a YugabyteDB Anywhere GCS storage configuration

### Synopsis

Manage a GCS storage configuration in YugabyteDB Anywhere

```
yba storage-config gcs [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the storage configuration for the operation. Required for create, delete, describe, update.
  -h, --help          help for gcs
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
* [yba storage-config gcs create](yba_storage-config_gcs_create.md)	 - Create a GCS YugabyteDB Anywhere storage configuration
* [yba storage-config gcs delete](yba_storage-config_gcs_delete.md)	 - Delete a GCS YugabyteDB Anywhere storage configuration
* [yba storage-config gcs describe](yba_storage-config_gcs_describe.md)	 - Describe a GCS YugabyteDB Anywhere storage configuration
* [yba storage-config gcs list](yba_storage-config_gcs_list.md)	 - List GCS YugabyteDB Anywhere storage-configurations
* [yba storage-config gcs update](yba_storage-config_gcs_update.md)	 - Update an GCS YugabyteDB Anywhere storage configuration

