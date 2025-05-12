## yba ldap disable

Disable LDAP authentication for YBA

### Synopsis

Disable LDAP authentication for YBA

```
yba ldap disable [flags]
```

### Examples

```
yba ldap disable --reset-fields ldap-host,ldap-port
```

### Options

```
      --reset-all              [Optional] Reset all LDAP fields to default values
      --reset-fields strings   [Optional] Reset specific LDAP fields to default values. Comma separated list of fields. Example: --reset-fields <field1>,<field2>
                                          Allowed values: ldap-host, ldap-port, ldap-ssl-protocol,tls-version, base-dn, dn-prefix, customer-uuid, search-and-bind-enabled, 
                                                          search-attribute, search-filter, service-account-dn, default-role,service-account-password, group-attribute, group-search-filter, 
                                                          group-search-base, group-search-scope, group-use-query, group-use-role-mapping
  -h, --help                   help for disable
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

* [yba ldap](yba_ldap.md)	 - Configure LDAP authentication for YBA

