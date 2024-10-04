## yba ear hashicorp update

Update a YugabyteDB Anywhere Hashicorp Vault encryption at rest (EAR) configuration

### Synopsis

Update a Hashicorp Vault encryption at rest (EAR) configuration in YugabyteDB Anywhere

```
yba ear hashicorp update [flags]
```

### Options

```
      --role-id string          [Optional] Update Hashicorp Vault AppRole ID.
      --secret-id string        [Optional] Update Hashicorp Vault AppRole Secret ID.
      --auth-namespace string   [Optional] Update Hashicorp Vault AppRole Auth Namespace.
      --token string            [Optional] Update Hashicorp Vault Token. Required if AppRole credentials are not provided.
  -h, --help                    help for update
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

* [yba ear hashicorp](yba_ear_hashicorp.md)	 - Manage a YugabyteDB Anywhere Hashicorp Vault encryption at rest (EAR) configuration

