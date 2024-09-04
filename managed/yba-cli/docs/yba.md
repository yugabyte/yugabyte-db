## yba

yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.

### Synopsis


	YugabyteDB Anywhere is a control plane for managing YugabyteDB universes
	across hybrid and multi-cloud environments, and provides automation and
	orchestration capabilities. YugabyteDB Anywhere CLI provides ease of access
	via the command line.

```
yba [flags]
```

### Options

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -h, --help               help for yba
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba auth](yba_auth.md)	 - Authenticate yba cli
* [yba backup](yba_backup.md)	 - Manage YugabyteDB Anywhere universe backups
* [yba completion](yba_completion.md)	 - Generate the autocompletion script for the specified shell
* [yba ear](yba_ear.md)	 - Manage YugabyteDB Anywhere Encryption at Rest Configurations
* [yba eit](yba_eit.md)	 - Manage YugabyteDB Anywhere Encryption In Transit (EIT) configurations
* [yba login](yba_login.md)	 - Authenticate yba cli using email and password
* [yba provider](yba_provider.md)	 - Manage YugabyteDB Anywhere providers
* [yba register](yba_register.md)	 - Register a YugabyteDB Anywhere customer using yba cli
* [yba storage-config](yba_storage-config.md)	 - Manage YugabyteDB Anywhere storage configurations
* [yba task](yba_task.md)	 - Manage YugabyteDB Anywhere tasks
* [yba universe](yba_universe.md)	 - Manage YugabyteDB Anywhere universes
* [yba yb-db-version](yba_yb-db-version.md)	 - Manage YugabyteDB version release

