## yba storage-config

Manage YugabyteDB Anywhere storage configurations

### Synopsis

Manage YugabyteDB Anywhere storage configurations

```
yba storage-config [flags]
```

### Options

```
  -h, --help   help for storage-config
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

* [yba](yba.md)	 - yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.
* [yba storage-config azure](yba_storage-config_azure.md)	 - Manage a YugabyteDB Anywhere Azure storage configuration
* [yba storage-config delete](yba_storage-config_delete.md)	 - Delete a YugabyteDB Anywhere storage configuration
* [yba storage-config describe](yba_storage-config_describe.md)	 - Describe a YugabyteDB Anywhere storage configuration
* [yba storage-config gcs](yba_storage-config_gcs.md)	 - Manage a YugabyteDB Anywhere GCS storage configuration
* [yba storage-config list](yba_storage-config_list.md)	 - List YugabyteDB Anywhere storage-configurations
* [yba storage-config nfs](yba_storage-config_nfs.md)	 - Manage a YugabyteDB Anywhere NFS storage configuration
* [yba storage-config s3](yba_storage-config_s3.md)	 - Manage a YugabyteDB Anywhere S3 storage configuration

