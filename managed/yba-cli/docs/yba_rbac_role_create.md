## yba rbac role create

Create YugabyteDB Anywhere RBAC roles

### Synopsis

Create YugabyteDB Anywhere RBAC roles

```
yba rbac role create [flags]
```

### Examples

```
yba rbac role create --name <role-name> \
	--permission resource-type=other::action=read \
	--description <description>
```

### Options

```
  -n, --name string              [Required] Name of the role. Quote name if it contains space.
      --description string       [Optional] Description of the role. Quote description if it contains space.
      --permission stringArray   [Required] Permissions associated with the role. Minimum number of required permissions = 1. Provide the following double colon (::) separated fields as key-value pairs: "resource-type=<resource-type>::action=<action>". Both are requires key-values. Allowed resource types are universe, role, user, other. Allowed actions are create, read, update, delete, pause_resume, backup_restore, update_role_bindings, update_profile, super_admin_actions, xcluster. Each permission needs to be added using a separate --permission flag. Example: --permission resource-type=other::action=delete --permission resource-type=universe::action=write
  -h, --help                     help for create
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

* [yba rbac role](yba_rbac_role.md)	 - Manage YugabyteDB Anywhere RBAC roles

