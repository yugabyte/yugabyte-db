## yba ldap configure

Configure LDAP authentication for YBA

### Synopsis

Configure/Update LDAP authentication for YBA

```
yba ldap configure [flags]
```

### Examples

```
yba ldap configure --ldap-host <ldap-host> --ldap-port <ldap-port> --base-dn '"<base-dn>"'
```

### Options

```
      --ldap-host string                  [Optional] LDAP server host
      --ldap-port int                     [Optional] LDAP server port (default 389)
      --ldap-ssl-protocol string          [Optional] LDAP SSL protocol. Allowed values: none, ldaps, starttls. (default "none")
      --ldap-tls-version string           [Optional] LDAP TLS version. Allowed values (case sensitive): TLSv1, TLSv1_1 and TLSv1_2. (default "TLSv1_2")
  -b, --base-dn string                    [Optional] Search base DN for LDAP. Must be enclosed in double quotes.
      --dn-prefix string                  [Optional] Prefix to be appended to the username for LDAP search.
                                                     Must be enclosed in double quotes. (default "CN=")
      --customer-uuid string              [Optional] YBA Customer UUID for LDAP authentication (Only for multi-tenant YBA)
      --search-and-bind                   [Optional] Use search and bind for LDAP Authentication. (default false).
      --ldap-search-attribute string      [Optional] LDAP search attribute for user authentication.
      --ldap-search-filter string         [Optional] LDAP search filter for user authentication.
                                                     Specify this or ldap-search-attribute for LDAP search and bind. Must be enclosed in double quotes
      --service-account-dn string         [Optional] Service account DN for LDAP authentication. Must be enclosed in double quotes
      --service-account-password string   [Optional] Service account password for LDAP authentication. Must be enclosed in double quotes
      --default-role string               [Optional] Default role for LDAP authentication.
                                                     This role will be used if a role cannot be determined via LDAP.
                                                     Allowed values (case sensitive): ReadOnly, ConnectOnly. (default "ReadOnly")
      --group-member-attribute string     [Optional] LDAP attribute that contains user groups.
                                                     Used for mapping LDAP groups to roles. (default "memberOf")
      --group-use-query                   [Optional] Enable LDAP query-based role mapping.
                                                     If set, the application will perform an LDAP search to determine a user's groups,
                                                     instead of using the attribute specified by --group-member-attribute. (default false).
      --group-search-filter string        [Optional] LDAP group search filter for role mapping. Must be enclosed in double quotes
      --group-search-base string          [Optional] LDAP group search base for role mapping. Must be enclosed in double quotes
      --group-search-scope string         [Optional] LDAP group search scope for role mapping.
                                                     Allowed values: OBJECT, ONELEVEL and SUBTREE. (default "SUBTREE")
  -h, --help                              help for configure
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

