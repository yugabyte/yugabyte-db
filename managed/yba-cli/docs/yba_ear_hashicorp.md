## yba ear hashicorp

Manage a YugabyteDB Anywhere Hashicorp Vault encryption at rest (EAR) configuration

### Synopsis

Manage a Hashicorp Vault encryption at rest (EAR) configuration in YugabyteDB Anywhere

```
yba ear hashicorp [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the configuration for the action. Required for create, delete, describe, update.
  -h, --help          help for hashicorp
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

* [yba ear](yba_ear.md)	 - Manage YugabyteDB Anywhere Encryption at Rest Configurations
* [yba ear hashicorp delete](yba_ear_hashicorp_delete.md)	 - Delete a YugabyteDB Anywhere Hashicorp Vault encryption at rest configuration
* [yba ear hashicorp describe](yba_ear_hashicorp_describe.md)	 - Describe a Hashicorp Vault YugabyteDB Anywhere Encryption In Transit (EAR) configuration
* [yba ear hashicorp list](yba_ear_hashicorp_list.md)	 - List Hashicorp Vault YugabyteDB Anywhere Encryption In Transit (EAR) configurations

