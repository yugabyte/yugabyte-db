## yba universe upgrade gflags

Gflags upgrade for a YugabyteDB Anywhere Universe

### Synopsis

Gflags upgrade for a YugabyteDB Anywhere Universe

```
yba universe upgrade gflags [flags]
```

### Options

```
      --add-master-gflags string               [Optional] Add Master GFlags to existing list. Provide comma-separated key-value pairs for the primary cluster in the following format: "--add-master-gflags master-gflag-key-1=master-gflag-value-1,master-gflag-key-2=master-gflag-key2".
      --edit-master-gflags string              [Optional] Edit Master GFlags in existing list. Provide comma-separated key-value pairs for the primary cluster in the following format: "--edit-master-gflags master-gflag-key-1=master-gflag-value-1,master-gflag-key-2=master-gflag-key2".
      --remove-master-gflags string            [Optional] Remove Master GFlags from existing list. Provide comma-separated values for the primary cluster in the following format: "--remove-master-gflags master-gflag-key-1,master-gflag-key-2".
      --inherit-from-primary                   [Optional] Apply the TServer GFlags changes of primary cluster to read replica cluster. (default true)
      --add-primary-tserver-gflags string      [Optional] Add TServer GFlags to primary cluster. Provide comma-separated key-value pairs in the following format: "--add-primary-tserver-gflags tserver-gflag-key-1-for-primary-cluster=tserver-gflag-value-1,tserver-gflag-key-2-for-primary-cluster=tserver-gflag-key2". If no-of-clusters = 2 and apply-to-read-replica is set to true, these gflags are copied to the read replica cluster.
      --edit-primary-tserver-gflags string     [Optional] Edit TServer GFlags in primary cluster. Provide comma-separated key-value pairs in the following format: "--edit-primary-tserver-gflags tserver-gflag-key-1-for-primary-cluster=tserver-gflag-value-1,tserver-gflag-key-2-for-primary-cluster=tserver-gflag-key2". If no-of-clusters = 2 and apply-to-read-replica is set to true, these gflag values are copied to the read replica cluster.
      --remove-primary-tserver-gflags string   [Optional] Remove TServer GFlags from primary cluster. Provide comma-separated keys in the following format: "--remove-primary-tserver-gflags tserver-gflag-key-1-for-primary-cluster,tserver-gflag-key-2-for-primary-cluster". If no-of-clusters = 2 and apply-to-read-replica is set to true, these gflag keys are removed from the read replica cluster.
      --add-rr-tserver-gflags string           [Optional] Add TServer GFlags to Read Replica cluster. Provide comma-separated key-value pairs in the following format: "--add-rr-tserver-gflags tserver-gflag-key-1-for-rr-cluster=tserver-gflag-value-1,tserver-gflag-key-2-for-rr-cluster=tserver-gflag-key2". Ignored if apply-to-read-replica is set to true.
      --edit-rr-tserver-gflags string          [Optional] Edit TServer GFlags in Read replica cluster. Provide comma-separated key-value pairs in the following format: "--edit-rr-tserver-gflags tserver-gflag-key-1-for-rr-cluster=tserver-gflag-value-1,tserver-gflag-key-2-for-rr-cluster=tserver-gflag-key2". Ignored if apply-to-read-replica is set to true.
      --remove-rr-tserver-gflags string        [Optional] Remove TServer GFlags from Read replica cluster. Provide comma-separated keys in the following format: "--remove-rr-tserver-gflags tserver-gflag-key-1-for-rr-cluster,tserver-gflag-key-2-for-rr-cluster". Ignored if apply-to-read-replica is set to true.
      --upgrade-option string                  [Optional] Upgrade Options, defaults to Rolling. Allowed values (case sensitive): Rolling, Non-Rolling (involves DB downtime), Non-Restart (default "Rolling")
      --delay-between-master-servers int32     [Optional] Upgrade delay between Master servers (in miliseconds). (default 18000)
      --delay-between-tservers int32           [Optional] Upgrade delay between Tservers (in miliseconds). (default 18000)
  -h, --help                                   help for gflags
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Required] The name of the universe to be ugraded.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe upgrade](yba_universe_upgrade.md)	 - Upgrade a YugabyteDB Anywhere universe

