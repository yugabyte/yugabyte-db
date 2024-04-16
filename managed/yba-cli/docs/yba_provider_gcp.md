## yba provider gcp

Manage a YugabyteDB Anywhere GCP provider

### Synopsis

Manage a GCP provider in YugabyteDB Anywhere

```
yba provider gcp [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the provider for the action. Required for create, delete, describe, update.
  -h, --help          help for gcp
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

* [yba provider](yba_provider.md)	 - Manage YugabyteDB Anywhere providers
* [yba provider gcp create](yba_provider_gcp_create.md)	 - Create a GCP YugabyteDB Anywhere provider
* [yba provider gcp delete](yba_provider_gcp_delete.md)	 - Delete a GCP YugabyteDB Anywhere provider
* [yba provider gcp describe](yba_provider_gcp_describe.md)	 - Describe a GCP YugabyteDB Anywhere provider
* [yba provider gcp list](yba_provider_gcp_list.md)	 - List GCP YugabyteDB Anywhere providers
* [yba provider gcp update](yba_provider_gcp_update.md)	 - Update a GCP YugabyteDB Anywhere provider

