## yba provider

Manage YugabyteDB Anywhere providers

### Synopsis

Manage YugabyteDB Anywhere providers

```
yba provider [flags]
```

### Options

```
  -h, --help   help for provider
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
* [yba provider aws](yba_provider_aws.md)	 - Manage a YugabyteDB Anywhere AWS provider
* [yba provider azure](yba_provider_azure.md)	 - Manage a YugabyteDB Anywhere Azure provider
* [yba provider delete](yba_provider_delete.md)	 - Delete a YugabyteDB Anywhere provider
* [yba provider describe](yba_provider_describe.md)	 - Describe a YugabyteDB Anywhere provider
* [yba provider gcp](yba_provider_gcp.md)	 - Manage a YugabyteDB Anywhere GCP provider
* [yba provider kubernetes](yba_provider_kubernetes.md)	 - Manage a YugabyteDB Anywhere K8s provider
* [yba provider list](yba_provider_list.md)	 - List YugabyteDB Anywhere providers
* [yba provider onprem](yba_provider_onprem.md)	 - Manage a YugabyteDB Anywhere on-premises provider

