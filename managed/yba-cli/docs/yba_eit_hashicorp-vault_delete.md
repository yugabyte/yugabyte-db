## yba eit hashicorp-vault delete

Delete a YugabyteDB Anywhere Hashicorp Vault encryption in transit configuration

### Synopsis

Delete a Hashicorp Vault encryption in transit configuration in YugabyteDB Anywhere

```
yba eit hashicorp-vault delete [flags]
```

### Examples

```
yba eit hashicorp-vault delete --name <config-name>
```

### Options

```
  -f, --force   [Optional] Bypass the prompt for non-interactive usage.
  -h, --help    help for delete
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the configuration for the action. Required for create, delete, describe, download, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba eit hashicorp-vault](yba_eit_hashicorp-vault.md)	 - Manage a YugabyteDB Anywhere Hashicorp Vault encryption in transit (EIT) certificate configuration

