## yba rbac role-binding delete

Delete a YugabyteDB Anywhere user role binding

### Synopsis

Delete a role binding from a YugabyteDB Anywhere user

```
yba rbac role-binding delete [flags]
```

### Examples

```
yba rbac role-binding delete --uuid <role-binding-uuid> --email <email>
```

### Options

```
  -u, --uuid string    [Required] UUID of the role binding to be deleted.
  -e, --email string   [Required] Email of the user whose role binding you want to delete.
  -h, --help           help for delete
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

* [yba rbac role-binding](yba_rbac_role-binding.md)	 - Manage YugabyteDB Anywhere RBAC role bindings

