## yba rbac role-binding

Manage YugabyteDB Anywhere RBAC role bindings

### Synopsis

Manage YugabyteDB Anywhere RBAC role bindings

```
yba rbac role-binding [flags]
```

### Options

```
  -h, --help   help for role-binding
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
* [yba rbac role-binding add](yba_rbac_role-binding_add.md)	 - Add a YugabyteDB Anywhere user role binding
* [yba rbac role-binding delete](yba_rbac_role-binding_delete.md)	 - Delete a YugabyteDB Anywhere user role binding
* [yba rbac role-binding describe](yba_rbac_role-binding_describe.md)	 - Describe a YugabyteDB Anywhere RBAC role binding
* [yba rbac role-binding list](yba_rbac_role-binding_list.md)	 - List YugabyteDB Anywhere role bindings

