## yba eit download client

Download YugabyteDB Anywhere Encryption In Transit (EIT) configuration's client certifciate.

### Synopsis

Download YugabyteDB Anywhere Encryption In Transit (EIT) configuration's client certificate. Cannot be used with certificate type K8SCertManager or CustomCertHostPath.

```
yba eit download client [flags]
```

### Examples

```
yba eit download client --name <config-name> --username <username>
```

### Options

```
      --username string   [Required] Connect to the database using this username for certificate-based authentication
  -h, --help              help for client
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
  -c, --cert-type string   [Optional] Type of the certificate. Client certifcates cannot be downloaded for K8sCertManager or CustomCertHostPath. Allowed values (case sensitive): SelfSigned, CustomCertHostPath, HashicorpVault, K8sCertManager.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Required] Name of the configuration.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba eit download](yba_eit_download.md)	 - Download YugabyteDB Anywhere Encryption In Transit (EIT) configuration's certifciates

