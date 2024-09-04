## yba eit

Manage YugabyteDB Anywhere Encryption In Transit (EIT) configurations

### Synopsis

Manage YugabyteDB Anywhere Encryption In Transit (EIT) configurations

```
yba eit [flags]
```

### Options

```
  -h, --help   help for eit
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
* [yba eit custom-ca](yba_eit_custom-ca.md)	 - Manage a YugabyteDB Anywhere Custom CA encryption in transit (EIT) certificate configuration
* [yba eit delete](yba_eit_delete.md)	 - Delete a YugabyteDB Anywhere encryption in transit configuration
* [yba eit describe](yba_eit_describe.md)	 - Describe a YugabyteDB Anywhere Encryption In Transit (EIT) configuration
* [yba eit hashicorp](yba_eit_hashicorp.md)	 - Manage a YugabyteDB Anywhere Hashicorp Vault encryption in transit (EIT) certificate configuration
* [yba eit k8s-cert-manager](yba_eit_k8s-cert-manager.md)	 - Manage a YugabyteDB Anywhere K8s Cert Manager encryption in transit (EIT) certificate configuration
* [yba eit list](yba_eit_list.md)	 - List YugabyteDB Anywhere Encryption In Transit (EIT) configurations
* [yba eit self-signed](yba_eit_self-signed.md)	 - Manage a YugabyteDB Anywhere Self Signed encryption in transit (EIT) certificate configuration

