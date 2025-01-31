## yba eit custom-ca download

Download a Custom CA YugabyteDB Anywhere Encryption In Transit (EIT) configuration's certifciates

### Synopsis

Download a Custom CA YugabyteDB Anywhere Encryption In Transit (EIT) configuration's certificates

```
yba eit custom-ca download [flags]
```

### Options

```
  -h, --help   help for download
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the configuration for the action. Required for create, delete, describe, download.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba eit custom-ca](yba_eit_custom-ca.md)	 - Manage a YugabyteDB Anywhere Custom CA encryption in transit (EIT) certificate configuration
* [yba eit custom-ca download root](yba_eit_custom-ca_download_root.md)	 - Download a Custom CA YugabyteDB Anywhere Encryption In Transit (EIT) configuration's root certifciate

