## yba backup restore create

Restore a YugabyteDB Anywhere universe backup

### Synopsis

Restore an universe backup in YugabyteDB Anywhere

```
yba backup restore create [flags]
```

### Options

```
      --universe-name string         [Required] Universe name. Name of the universe to be backed up
      --storage-config-name string   [Required] Storage config to be used for taking the backup
      --keyspace-info stringArray    [Required] Keyspace info to perform restore operation.If no keyspace info is provided, then all the keyspaces of the table type specified are backed up. If the user wants to take backup of a subset of keyspaces, then the user has to specify the keyspace info. Provide the following semicolon separated fields as key value pairs: "keyspace-name=<keyspace-name>;storage-location=<storage_location>;backup-type=<ycql/ysql>;use-tablespaces=<use-tablespaces>;selective-restore=<selective-restore>;table-name-list=<table-name1>,<table-name2> The table-name-list attribute has to be specified as comma separated values.Keyspace name, storage-location and backup-type are required values. The attribute use-tablespaces, selective-restore and table-name-list are optional values. Attributes selective-restore and table-name-list and are needed only for YCQL. The attribute use-tablespaces is to be used if needed only in the case of YSQLExample: --keyspace-info keyspace-name=cassandra1;storage-location=s3://bucket/location1;backup-type=ycql;selective-restore=true;table-name-list=table1,table2 --keyspace-info keyspace-name=postgres;storage-location=s3://bucket/location2backup-type=ysql;use-tablespaces=true
  -h, --help                         help for create
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

* [yba backup restore](yba_backup_restore.md)	 - Manage YugabyteDB Anywhere universe backup restores

