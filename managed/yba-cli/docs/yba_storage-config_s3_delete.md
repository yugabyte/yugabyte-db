## yba storage-config s3 delete

Delete a S3 YugabyteDB Anywhere storage configuration

### Synopsis

Delete a S3 storage configuration in YugabyteDB Anywhere

```
yba storage-config s3 delete [flags]
```

### Options

```
  -f, --force   [Optional] Bypass the prompt for non-interactive usage.
  -h, --help    help for delete
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

* [yba storage-config s3](yba_storage-config_s3.md)	 - Manage a YugabyteDB Anywhere S3 storage configuration

