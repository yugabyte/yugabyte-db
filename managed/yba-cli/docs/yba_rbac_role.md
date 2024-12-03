## yba rbac role

Manage YugabyteDB Anywhere RBAC roles

### Synopsis

Manage YugabyteDB Anywhere RBAC roles

```
yba rbac role [flags]
```

### Options

```
  -h, --help   help for role
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

* [yba rbac](yba_rbac.md)	 - Manage YugabyteDB Anywhere RBAC (Role-Based Access Control)
* [yba rbac role create](yba_rbac_role_create.md)	 - Create YugabyteDB Anywhere RBAC roles
* [yba rbac role delete](yba_rbac_role_delete.md)	 - Delete a YugabyteDB Anywhere role
* [yba rbac role describe](yba_rbac_role_describe.md)	 - Describe a YugabyteDB Anywhere RBAC role
* [yba rbac role list](yba_rbac_role_list.md)	 - List YugabyteDB Anywhere roles
* [yba rbac role update](yba_rbac_role_update.md)	 - Update a YugabyteDB Anywhere role

