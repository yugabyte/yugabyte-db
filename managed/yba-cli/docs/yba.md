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
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -h, --help               help for yba
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba alert](yba_alert.md)	 - Manage YugabyteDB Anywhere alerts
* [yba auth](yba_auth.md)	 - Authenticate yba cli
* [yba backup](yba_backup.md)	 - Manage YugabyteDB Anywhere universe backups
* [yba completion](yba_completion.md)	 - Generate the autocompletion script for the specified shell
* [yba customer](yba_customer.md)	 - Manage YugabyteDB Anywhere customers
* [yba ear](yba_ear.md)	 - Manage YugabyteDB Anywhere Encryption at Rest Configurations
* [yba eit](yba_eit.md)	 - Manage YugabyteDB Anywhere Encryption In Transit (EIT) configurations
* [yba group](yba_group.md)	 - Manage YugabyteDB Anywhere groups
* [yba host](yba_host.md)	 - Refer to YugabyteDB Anywhere host details
* [yba ldap](yba_ldap.md)	 - Configure LDAP authentication for YBA
* [yba login](yba_login.md)	 - Authenticate yba cli using email and password
* [yba oidc](yba_oidc.md)	 - Manage YugabyteDB Anywhere OIDC configuration
* [yba provider](yba_provider.md)	 - Manage YugabyteDB Anywhere providers
* [yba rbac](yba_rbac.md)	 - Manage YugabyteDB Anywhere RBAC (Role-Based Access Control)
* [yba register](yba_register.md)	 - Register a YugabyteDB Anywhere customer using yba cli
* [yba runtime-config](yba_runtime-config.md)	 - Manage YugabyteDB Anywhere runtime configurations
* [yba storage-config](yba_storage-config.md)	 - Manage YugabyteDB Anywhere storage configurations
* [yba task](yba_task.md)	 - Manage YugabyteDB Anywhere tasks
* [yba telemetry-provider](yba_telemetry-provider.md)	 - Manage YugabyteDB Anywhere telemetry providers
* [yba tree](yba_tree.md)	 - Visualize the command tree
* [yba universe](yba_universe.md)	 - Manage YugabyteDB Anywhere universes
* [yba user](yba_user.md)	 - Manage YugabyteDB Anywhere users
* [yba xcluster](yba_xcluster.md)	 - Manage YugabyteDB Anywhere xClusters
* [yba yb-db-version](yba_yb-db-version.md)	 - Manage YugabyteDB versions

