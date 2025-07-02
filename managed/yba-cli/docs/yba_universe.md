## yba universe

Manage YugabyteDB Anywhere universes

### Synopsis

Manage YugabyteDB Anywhere universes

```
yba universe [flags]
```

### Options

```
  -h, --help   help for universe
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

* [yba](yba.md)	 - yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.
* [yba universe attach](yba_universe_attach.md)	 - Attach universe to a destination YugabyteDB Anywhere
* [yba universe create](yba_universe_create.md)	 - Create YugabyteDB Anywhere universe
* [yba universe create-read-replica](yba_universe_create-read-replica.md)	 - Create a read replica for existing YugabyteDB universe
* [yba universe delete](yba_universe_delete.md)	 - Delete a YugabyteDB Anywhere universe
* [yba universe delete-metadata](yba_universe_delete-metadata.md)	 - Delete metadata of a universe from a YugabyteDB Anywhere
* [yba universe delete-read-replica](yba_universe_delete-read-replica.md)	 - Delete a YugabyteDB Anywhere universe Read Replica
* [yba universe describe](yba_universe_describe.md)	 - Describe a YugabyteDB Anywhere universe
* [yba universe detach](yba_universe_detach.md)	 - Detach a universe from a YugabyteDB Anywhere
* [yba universe edit](yba_universe_edit.md)	 - Edit a YugabyteDB Anywhere universe
* [yba universe list](yba_universe_list.md)	 - List YugabyteDB Anywhere universes
* [yba universe node](yba_universe_node.md)	 - Manage YugabyteDB Anywhere universe nodes
* [yba universe pause](yba_universe_pause.md)	 - Pause a YugabyteDB Anywhere universe
* [yba universe restart](yba_universe_restart.md)	 - Restart a YugabyteDB Anywhere Universe
* [yba universe resume](yba_universe_resume.md)	 - Resume a YugabyteDB Anywhere universe
* [yba universe run-sample-apps](yba_universe_run-sample-apps.md)	 - Get sample apps command for a YugabyteDB Anywhere universe
* [yba universe security](yba_universe_security.md)	 - Manage security settings for a universe
* [yba universe support-bundle](yba_universe_support-bundle.md)	 - Support bundle operations on a YugabyteDB Anywhere universe
* [yba universe table](yba_universe_table.md)	 - Manage YugabyteDB Anywhere universe tables
* [yba universe upgrade](yba_universe_upgrade.md)	 - Upgrade a YugabyteDB Anywhere universe

