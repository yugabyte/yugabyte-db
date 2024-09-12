## yba eit self-signed create

Create a YugabyteDB Anywhere Self Signed encryption in transit configuration

### Synopsis

Create a Self Signed encryption in transit configuration in YugabyteDB Anywhere

```
yba eit self-signed create [flags]
```

### Options

```
      --root-cert-file-path string   [Required] Root certificate file path.
      --key-file-path string         [Required] Private key file path.
  -h, --help                         help for create
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

* [yba eit self-signed](yba_eit_self-signed.md)	 - Manage a YugabyteDB Anywhere Self Signed encryption in transit (EIT) certificate configuration

