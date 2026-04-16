## yba ear

Manage YugabyteDB Anywhere Encryption at Rest Configurations

### Synopsis

Manage YugabyteDB Anywhere Encryption at Rest Configurations

```
yba ear [flags]
```

### Options

```
  -h, --help   help for ear
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
* [yba ear aws](yba_ear_aws.md)	 - Manage a YugabyteDB Anywhere AWS encryption at rest (EAR) configuration
* [yba ear azure](yba_ear_azure.md)	 - Manage a YugabyteDB Anywhere Azure encryption at rest (EAR) configuration
* [yba ear ciphertrust](yba_ear_ciphertrust.md)	 - Manage a YugabyteDB Anywhere CipherTrust encryption at rest (EAR) configuration
* [yba ear delete](yba_ear_delete.md)	 - Delete a YugabyteDB Anywhere encryption at rest configuration
* [yba ear describe](yba_ear_describe.md)	 - Describe a YugabyteDB Anywhere Encryption At Rest (EAR) configuration
* [yba ear gcp](yba_ear_gcp.md)	 - Manage a YugabyteDB Anywhere GCP encryption at rest (EAR) configuration
* [yba ear hashicorp-vault](yba_ear_hashicorp-vault.md)	 - Manage a YugabyteDB Anywhere Hashicorp Vault encryption at rest (EAR) configuration
* [yba ear list](yba_ear_list.md)	 - List YugabyteDB Anywhere Encryption at Rest Configurations
* [yba ear refresh](yba_ear_refresh.md)	 - Refresh a YugabyteDB Anywhere Encryption At Rest (EAR) configuration

