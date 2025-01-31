## yba rbac role-binding add

Add a YugabyteDB Anywhere user role binding

### Synopsis

Add a role binding to a YugabyteDB Anywhere user

```
yba rbac role-binding add [flags]
```

### Examples

```
yba rbac role-binding add --email <email> \
	--role-resource-definition role-uuid=<role-uuid1>::resource-type=<resource-type1>:: \
	resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>
```

### Options

```
  -e, --email string                           [Required] Email of the user to add role binding
      --role-resource-definition stringArray   [Optional] Role resource bindings to be added.  Provide the following double colon (::) separated fields as key-value pairs:"role-uuid=<role-uuid>::allow-all=<true/false>::resource-type=<resource-type>::resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>". Role UUID is a required value. Allowed values for resource type are universe, role, user, other. If resource UUID list is empty, default for allow all is true. If the role given is a system role, resource type, allow all and resource UUID must be empty. Add multiple resource types for each role UUID  using separate --role-resource-definition flags. Each binding needs to be added using a separate --role-resource-definition flag. Example: --role-resource-definition role-uuid=<role-uuid1>::resource-type=<resource-type1>::resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3> --role-resource-definition role-uuid=<role-uuid2>::resource-type=<resource-type1>::resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3> --role-resource-definition role-uuid=<role-uuid2>::resource-type=<resource-type2>::resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>
  -h, --help                                   help for add
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

