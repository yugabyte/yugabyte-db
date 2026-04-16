## yba oidc configure

Configure OIDC configuration for YBA

### Synopsis

Configure OIDC configuration for YBA

```
yba oidc configure [flags]
```

### Examples

```
yba oidc configure --client-id <client-id> --client-secret <client-secret> --discovery-url <discovery-url>
```

### Options

```
      --client-id string                          OIDC client ID. Required if not already configured
      --client-secret string                      OIDC client secret associated with the client ID. Required if not already configured.
      --discovery-url string                      [Optional] URL to the OIDC provider's discovery document. Must be enclosed in double quotes.
                                                             Typically ends with '/.well-known/openid-configuration'.
                                                             Either this or the provider configuration document must be configured.
      --scope string                              [Optional] Space-separated list of scopes to request from the identity provider. Enclosed in quotes. (default "openid profile email")
      --email-attribute string                    [Optional] Claim name from which to extract the user's email address.
      --refresh-token-url string                  [Optional] Endpoint used to refresh the access token from the OIDC provider.
      --provider-configuration string             [Optional] JSON string representing the full OIDC provider configuration document.
                                                   Provide either this or the --provider-configuration-file-path flag.
      --provider-configuration-file-path string   [Optional] Path to the file containing the full OIDC provider configuration document.
                                                   Provide either this or the --provider-configuration flag.
      --auto-create-user                          [Optional] Whether to automatically create a user in YBA if one does not exist.
                                                             If set, a new user will be created upon successful OIDC login. (default true)
      --default-role string                       [Optional] Default role to assign when a role cannot be determined via OIDC.
                                                             Allowed values (case sensitive): ReadOnly, ConnectOnly. (default "ReadOnly")
      --group-claim string                        [Optional] Name of the claim in the ID token or user info response that lists user groups. (default "groups")
  -h, --help                                      help for configure
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

* [yba oidc](yba_oidc.md)	 - Manage YugabyteDB Anywhere OIDC configuration

