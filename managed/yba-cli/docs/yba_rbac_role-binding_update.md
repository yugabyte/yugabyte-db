## yba rbac role-binding update

Update a YugabyteDB Anywhere role binding

### Synopsis

Update a role binding in YugabyteDB Anywhere

```
yba rbac role-binding update [flags]
```

### Examples

```
yba rbac role-binding update 
```

### Options

```
  -u, --uuid string                     [Required] UUID of the role binding to be updated.
      --add-permission stringArray      [Optional] Add permissions to the s. Provide the following double colon (::) separated fields as key-value pairs: "resource-type=<resource-type>::action=<action>". Both are requires key-values. Allowed resource types are: universe, role, user, other. Allowed actions are: create, read, update, delete, pause_resume, backup_restore, update_role_bindings, update_profile, super_admin_actions, xcluster.Quote action if it contains space. Each permission needs to be added using a separate --add-permission flag.
      --remove-permission stringArray   [Optional] Remove permissions from the role. Provide the following double colon (::) separated fields as key-value pairs: "resource-type=<resource-type>::action=<action>". Both are requires key-values. Allowed resource types are: universe, role, user, other. Allowed actions are: create, read, update, delete, pause_resume, backup_restore, update_role_bindings, update_profile, super_admin_actions, xcluster.Quote action if it contains space. Each permission needs to be removed using a separate --remove-permission flag.
  -h, --help                            help for update
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

