## yba backup restore create

Restore a YugabyteDB Anywhere universe backup

### Synopsis

Restore an universe backup in YugabyteDB Anywhere

```
yba backup restore create [flags]
```

### Examples

```
yba backup restore create --universe-name <universe-name> \
	--storage-config-name <storage-config-name> \
	--keyspace-info \
	keyspace-name=<keyspace-name-1>::storage-location=<storage-location-1>::backup-type=ysql \
	--keyspace-info \
	keyspace-name=<keyspace-name-2>::storage-location=<storage-location-2>::backup-type=ysql
```

### Options

```
      --universe-name string         [Required] Target universe name to perform restore operation.
      --storage-config-name string   [Required] Storage config to be used for taking the backup
      --kms-config string            [Optional] Key management service config name. For a successful restore, the KMS configuration used for restore should be the same KMS configuration used during backup creation.
      --keyspace-info stringArray    [Required] Keyspace info to perform restore operation. Provide the following double colon (::) separated fields as key value pairs: "keyspace-name=<keyspace-name>::storage-location=<storage_location>::backup-type=<ycql/ysql>::use-tablespaces=<use-tablespaces>::selective-restore=<selective-restore>::table-name-list=<table-name1>,<table-name2>". The table-name-list attribute has to be specified as comma separated values. Keyspace name, storage-location and backup-type are required values. The attributes use-tablespaces, selective-restore and table-name-list are optional. Attributes selective-restore and table-name-list are needed only for YCQL. The attribute use-tablespaces is needed only for YSQL. Example: --keyspace-info keyspace-name=cassandra1::storage-location=s3://bucket/location1::backup-type=ycql::selective-restore=true::table-name-list=table1,table2 --keyspace-info keyspace-name=postgres::storage-location=s3://bucket/location2backup-type=ysql::use-tablespaces=true
      --enable-verbose-logs          [Optional] Enable verbose logging while taking backup via "yb_backup" script. (default false)
      --parallelism int              [Optional] Number of concurrent commands to run on nodes over SSH via "yb_backup" script. (default 8)
  -h, --help                         help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba backup restore](yba_backup_restore.md)	 - Manage YugabyteDB Anywhere universe backup restores

