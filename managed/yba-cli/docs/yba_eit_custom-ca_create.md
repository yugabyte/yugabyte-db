## yba eit custom-ca create

Create a YugabyteDB Anywhere Custom CA encryption in transit configuration

### Synopsis

Create a Custom CA encryption in transit configuration in YugabyteDB Anywhere

```
yba eit custom-ca create [flags]
```

### Options

```
      --root-cert-file-path string             [Required] Root certificate file path to upload.
      --root-ca-file-path-on-node string       [Required] Root CA certificate file path on the on-premises node.
      --node-cert-file-path-on-node string     [Required] Node certificate file path on the on-premises node.
      --node-key-file-path-on-node string      [Required] Node key file path on the on-premises node.
      --client-cert-file-path-on-node string   [Optional] Client certificate file path on the on-premises node to enable client-to-node TLS.
      --client-key-file-path-on-node string    [Optional] Client key file path on the on-premises node to enable client-to-node TLS.
  -h, --help                                   help for create
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

* [yba eit custom-ca](yba_eit_custom-ca.md)	 - Manage a YugabyteDB Anywhere Custom CA encryption in transit (EIT) certificate configuration

