## yba ear

Manage YugabyteDB Anywhere Encryption at Rest Configurations

### Synopsis

Manage YugabyteDB Anywhere Encryption at Rest Configurations

```
yba ear [flags]
```

### Options

```
  -h, --help   help for ear
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

* [yba](yba.md)	 - yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.
* [yba ear aws](yba_ear_aws.md)	 - Manage a YugabyteDB Anywhere AWS encryption at rest (EAR) configuration
* [yba ear azure](yba_ear_azure.md)	 - Manage a YugabyteDB Anywhere Azure encryption at rest (EAR) configuration
* [yba ear delete](yba_ear_delete.md)	 - Delete a YugabyteDB Anywhere encryption at rest configuration
* [yba ear describe](yba_ear_describe.md)	 - Describe a YugabyteDB Anywhere Encryption At Rest (EAR) configuration
* [yba ear gcp](yba_ear_gcp.md)	 - Manage a YugabyteDB Anywhere GCP encryption at rest (EAR) configuration
* [yba ear hashicorp](yba_ear_hashicorp.md)	 - Manage a YugabyteDB Anywhere Hashicorp Vault encryption at rest (EAR) configuration
* [yba ear list](yba_ear_list.md)	 - List YugabyteDB Anywhere Encryption at Rest Configurations

