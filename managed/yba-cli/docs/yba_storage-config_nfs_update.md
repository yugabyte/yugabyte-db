## yba storage-config nfs update

Update an NFS YugabyteDB Anywhere storage configuration

### Synopsis

Update an NFS storage configuration in YugabyteDB Anywhere

```
yba storage-config nfs update [flags]
```

### Examples

```
yba storage-config nfs update --name <storage-configuration-name> \
	--new-name <new-storage-configuration-name>
```

### Options

```
      --new-name string   [Optional] Update name of the storage configuration.
  -h, --help              help for update
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

* [yba storage-config nfs](yba_storage-config_nfs.md)	 - Manage a YugabyteDB Anywhere NFS storage configuration

