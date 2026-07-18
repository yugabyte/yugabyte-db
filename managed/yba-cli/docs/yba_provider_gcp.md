## yba provider gcp

Manage a YugabyteDB Anywhere GCP provider

### Synopsis

Manage a GCP provider in YugabyteDB Anywhere

```
yba provider gcp [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the provider for the action. Required for create, delete, describe, update and some instance-type subcommands.
  -h, --help          help for gcp
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
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba provider](yba_provider.md)	 - Manage YugabyteDB Anywhere providers
* [yba provider gcp create](yba_provider_gcp_create.md)	 - Create a GCP YugabyteDB Anywhere provider
* [yba provider gcp delete](yba_provider_gcp_delete.md)	 - Delete a GCP YugabyteDB Anywhere provider
* [yba provider gcp describe](yba_provider_gcp_describe.md)	 - Describe a GCP YugabyteDB Anywhere provider
* [yba provider gcp instance-type](yba_provider_gcp_instance-type.md)	 - Manage YugabyteDB Anywhere GCP instance types
* [yba provider gcp list](yba_provider_gcp_list.md)	 - List GCP YugabyteDB Anywhere providers
* [yba provider gcp update](yba_provider_gcp_update.md)	 - Update a GCP YugabyteDB Anywhere provider

