## yba ear hashicorp-vault list

List Hashicorp Vault YugabyteDB Anywhere Encryption In Transit (EAR) configurations

### Synopsis

List Hashicorp Vault YugabyteDB Anywhere Encryption In Transit (EAR) configurations

```
yba ear hashicorp-vault list [flags]
```

### Examples

```
yba ear hashicorp-vault list
```

### Options

```
  -h, --help   help for list
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the configuration for the action. Required for create, delete, describe, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba ear hashicorp-vault](yba_ear_hashicorp-vault.md)	 - Manage a YugabyteDB Anywhere Hashicorp Vault encryption at rest (EAR) configuration

