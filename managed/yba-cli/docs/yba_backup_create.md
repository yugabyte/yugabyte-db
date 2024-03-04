## yba backup create

Create a YugabyteDB Anywhere universe backup

### Synopsis

Create an universe backup in YugabyteDB Anywhere

```
yba backup create [flags]
```

### Options

```
      --universe-name string           [Required] Universe name. Name of the universe to be backed up
      --storage-config-name string     [Required] Storage config to be used for taking the backup
      --table-type string              [Required] Table type. Allowed values: ysql, ycql, yedis
      --time-before-delete-in-ms int   [Optional] Retention time of the backup in milliseconds
      --keyspace-info stringArray      [Optional] Keyspace info to perform backup operation.If no keyspace info is provided, then all the keyspaces of the table type specified are backed up. If the user wants to take backup of a subset of keyspaces, then the user has to specify the keyspace info. Provide the following semicolon separated fields as key value pairs: "keyspace-name=<keyspace-name>;table-names=<table-name1>,<table-name2>,<table-name3>;table-ids=<table-id1>,<table-id2>,<table-id3>". The table-names and table-ids attributes have to be specified as comma separated values.Keyspace name is required value. Table names and Table ids are optional values and are needed only for YCQL.Example: --keyspace-info keyspace-name=cassandra;table-names=table1,table2;table-ids=1e683b86-7858-44d1-a1f6-406f50a4e56e,19a34a5e-3a19-4070-9d79-805ed713ce7d --keyspace-info keyspace-name=cassandra2;table-names=table3,table4;table-ids=e5b83a7c-130c-40c0-95ff-ec1d9ecff616,bc92d473-2e10-4f76-8bd1-9ca9741890fd
      --use-tablespaces                [Optional] Backup tablespaces information as part of the backup
      --sse                            [Optional] Enable sse while persisting the data in AWS S3 (default true)
      --base-backup-uuid string        [Optional] Base Backup UUID for taking incremental backups
  -h, --help                           help for create
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

* [yba backup](yba_backup.md)	 - Manage YugabyteDB Anywhere backups

