## yba eit download

Download YugabyteDB Anywhere Encryption In Transit (EIT) configuration's certifciates

### Synopsis

Download YugabyteDB Anywhere Encryption In Transit (EIT) configuration's certificate

```
yba eit download [flags]
```

### Options

```
  -c, --cert-type string   [Optional] Type of the certificate. Client certifcates cannot be downloaded for K8sCertManager or CustomCertHostPath. Allowed values (case sensitive): SelfSigned, CustomCertHostPath, HashicorpVault, K8sCertManager.
  -n, --name string        [Required] Name of the configuration.
  -h, --help               help for download
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

* [yba eit](yba_eit.md)	 - Manage YugabyteDB Anywhere Encryption In Transit (EIT) configurations
* [yba eit download client](yba_eit_download_client.md)	 - Download YugabyteDB Anywhere Encryption In Transit (EIT) configuration's client certifciate.
* [yba eit download root](yba_eit_download_root.md)	 - Download YugabyteDB Anywhere Encryption In Transit (EIT) configuration's root certifciate

