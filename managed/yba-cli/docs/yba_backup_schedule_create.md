## yba backup schedule create

Create a YugabyteDB Anywhere universe backup schedule

### Synopsis

Create an universe backup schedule in YugabyteDB Anywhere

```
yba backup schedule create [flags]
```

### Examples

```
yba backup schedule create -n <schedule-name> \
	  --schedule-frequency-in-secs <schedule-frequency-in-secs> \
	  --universe-name <universe-name> --storage-config-name <storage-config-name> \
	  --table-type <table-type>
```

### Options

```
  -n, --name string                                [Required] Schedule name. Name of the schedule to perform universe backups
      --cron-expression string                     [Optional] Cron expression to manage the backup schedules. Provide either cron-expression or schedule-frequency-in-secs. If both are provided, cron-expression takes precedence.
      --schedule-frequency-in-secs int             [Optional] Backup frequency to manage the backup schedules. Provide either schedule-frequency-in-secs or cron-expression. If both are provided, cron-expression takes precedence.
      --incremental-backup-frequency-in-secs int   [Optional] Incremental backup frequency to manage the incremental backup schedules
      --universe-name string                       [Required] Universe name. Name of the universe to be backed up
      --storage-config-name string                 [Required] Storage config to be used for taking the backup
      --table-type string                          [Required] Table type. Allowed values: ysql, ycql, yedis
      --time-before-delete-in-ms int               [Optional] Retention time of the backup in milliseconds
      --keyspace-info stringArray                  [Optional] Keyspace info to perform backup operation. If no keyspace info is provided, then all the keyspaces of the table type specified are backed up. If the user wants to take backup of a subset of keyspaces, then the user has to specify the keyspace info. Provide the following double colon (::) separated fields as key value pairs: "keyspace-name=<keyspace-name>::table-names=<table-name1>,<table-name2>,<table-name3>::table-ids=<table-id1>,<table-id2>,<table-id3>". The table-names and table-ids attributes have to be specified as comma separated values.Keyspace name is required value. Table names and Table IDs/UUIDs are optional values and are needed only for YCQL.Example: --keyspace-info keyspace-name=cassandra::table-names=table1,table2::table-ids=1e683b86-7858-44d1-a1f6-406f50a4e56e,19a34a5e-3a19-4070-9d79-805ed713ce7d --keyspace-info keyspace-name=cassandra2::table-names=table3,table4::table-ids=e5b83a7c-130c-40c0-95ff-ec1d9ecff616,bc92d473-2e10-4f76-8bd1-9ca9741890fd
      --use-tablespaces                            [Optional] Backup tablespaces information as part of the backup.
      --category string                            [Optional] Category of the backup. If a universe has YBC enabled, then default value of category is YB_CONTROLLER. Allowed values: yb_backup_script, yb_controller
      --sse                                        [Optional] Enable sse while persisting the data in AWS S3. (default true)
      --enable-verbose-logs                        [Optional] Enable verbose logging while taking backup via "yb_backup" script. (default false)
      --parallelism int                            [Optional] Number of concurrent commands to run on nodes over SSH via "yb_backup" script. (default 8)
      --enable-pitr                                [Optional] Enable PITR for the backup schedule. (default false)
  -h, --help                                       help for create
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

* [yba backup schedule](yba_backup_schedule.md)	 - Manage YugabyteDB Anywhere universe backup schedules

