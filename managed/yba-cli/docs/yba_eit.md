## yba eit

Manage YugabyteDB Anywhere Encryption In Transit (EIT) configurations

### Synopsis

Manage YugabyteDB Anywhere Encryption In Transit (EIT) configurations

```
yba eit [flags]
```

### Options

```
  -h, --help   help for eit
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

* [yba](yba.md)	 - yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.
* [yba eit custom-ca](yba_eit_custom-ca.md)	 - Manage a YugabyteDB Anywhere Custom CA encryption in transit (EIT) certificate configuration
* [yba eit delete](yba_eit_delete.md)	 - Delete a YugabyteDB Anywhere encryption in transit configuration
* [yba eit describe](yba_eit_describe.md)	 - Describe a YugabyteDB Anywhere Encryption In Transit (EIT) configuration
* [yba eit download](yba_eit_download.md)	 - Download YugabyteDB Anywhere Encryption In Transit (EIT) configuration's certifciates
* [yba eit hashicorp-vault](yba_eit_hashicorp-vault.md)	 - Manage a YugabyteDB Anywhere Hashicorp Vault encryption in transit (EIT) certificate configuration
* [yba eit k8s-cert-manager](yba_eit_k8s-cert-manager.md)	 - Manage a YugabyteDB Anywhere K8s Cert Manager encryption in transit (EIT) certificate configuration
* [yba eit list](yba_eit_list.md)	 - List YugabyteDB Anywhere Encryption In Transit (EIT) configurations
* [yba eit self-signed](yba_eit_self-signed.md)	 - Manage a YugabyteDB Anywhere Self Signed encryption in transit (EIT) certificate configuration

