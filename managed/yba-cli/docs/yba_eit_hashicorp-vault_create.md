## yba eit hashicorp-vault create

Create a YugabyteDB Anywhere Hashicorp Vault encryption in transit configuration

### Synopsis

Create a Hashicorp Vault encryption in transit configuration in YugabyteDB Anywhere

```
yba eit hashicorp-vault create [flags]
```

### Examples

```
yba eit hashicorp-vault create --name <config-name> \
	--token <token> --vault-address <vault-address> \
	--secret-engine <secret-engine>
```

### Options

```
      --vault-address string   Hashicorp Vault address. Can also be set using environment variable VAULT_ADDR
      --token string           Hashicorp Vault Token. Can also be set using environment variable VAULT_TOKEN
      --secret-engine string   [Optional] Hashicorp Vault Secret Engine. Allowed values: pki. (default "pki")
      --role string            [Required] The role used for creating certificates in Hashicorp Vault.
      --mount-path string      [Optional] Hashicorp Vault mount path. (default "pki/")
  -h, --help                   help for create
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
  -n, --name string        [Optional] The name of the configuration for the action. Required for create, delete, describe, download, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba eit hashicorp-vault](yba_eit_hashicorp-vault.md)	 - Manage a YugabyteDB Anywhere Hashicorp Vault encryption in transit (EIT) certificate configuration

