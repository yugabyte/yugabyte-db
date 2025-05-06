## yba group update

Update a group

### Synopsis

Update group mapping for LDAP/OIDC

```
yba group update [flags]
```

### Examples

```
yba group update -n <group-name> --role-resource-definition <role-resource-definition>
```

### Options

```
  -n, --name string                                [Required] Name of the group to update.
      --role-resource-definition stringArray       [Optional] Set the exact list of roles-definitions for the group, replacing any existing roles.
                                                   		 Provide the following double colon (::) separated fields as key-value pairs:
                                                   		 "role-uuid=<role-uuid>::allow-all=<true/false>::resource-type=<resource-type>::
                                                   		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>".
                                                   		 Role UUID is a required value. 
                                                   		 Allowed values for resource type are universe, role, user, other.
                                                   		 If resource UUID list is empty, allow all is set to true.
                                                   		 If the role given is a system role, resource type, allow all and
                                                   		 resource UUID must be empty.
                                                   		 Add multiple resource types for each role UUID
                                                   		 using separate --role-resource-definition flags.
                                                   		 Each binding needs to be added using a separate --role-resource-definition flag.
                                                   		 Example: --role-resource-definition
                                                   		 role-uuid=<role-uuid1>::resource-type=<resource-type1>::
                                                   		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>
                                                   		 --role-resource-definition
                                                   		 role-uuid=<role-uuid2>::resource-type=<resource-type1>::
                                                   		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>
                                                   		 --role-resource-definition
                                                   		 role-uuid=<role-uuid2>::resource-type=<resource-type2>::
                                                   		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>
      --add-role-resource-definition stringArray   [Optional] Add one or more role-resource-definitions to the group.
                                                   		 Input format is same as --role-resource-definition.
                                                   		 Example: --add-role-resource-definition role-uuid=<role-uuid1>::resource-type=<resource-type1>::
                                                   		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>
                                                   		 --add-role-resource-definition role-uuid=<role-uuid2>::resource-type=<resource-type1>::
                                                   		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3> 
      --remove-roles strings                       [Optional] Remove one or more roles from the group. 
                                                   Example: --remove-roles role1,role2 or --remove-roles role1 --remove-roles role2.
  -h, --help                                       help for update
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

* [yba group](yba_group.md)	 - Manage YugabyteDB Anywhere groups

