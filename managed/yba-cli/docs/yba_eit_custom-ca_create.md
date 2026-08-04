## yba eit custom-ca create

Create a YugabyteDB Anywhere Custom CA encryption in transit configuration

### Synopsis

Create a Custom CA encryption in transit configuration in YugabyteDB Anywhere

```
yba eit custom-ca create [flags]
```

### Examples

```
yba eit custom-ca create --name <config-name> \
	--root-cert-file-path <root-cert-file-path> \
	--root-ca-file-path-on-node <root-ca-file-path-on-node> \
	--node-cert-file-path-on-node <node-cert-file-path-on-node> \
	--node-key-file-path-on-node <node-key-file-path-on-node> \
	--client-cert-file-path-on-node <client-cert-file-path-on-node> \
	--client-key-file-path-on-node <client-key-file-path-on-node>
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
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the configuration for the action. Required for create, delete, describe, download.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba eit custom-ca](yba_eit_custom-ca.md)	 - Manage a YugabyteDB Anywhere Custom CA encryption in transit (EIT) certificate configuration

