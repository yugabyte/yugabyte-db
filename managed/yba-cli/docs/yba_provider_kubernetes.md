## yba provider kubernetes

Manage a YugabyteDB Anywhere K8s provider

### Synopsis

Manage a K8s provider in YugabyteDB Anywhere

```
yba provider kubernetes [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the provider for the action. Required for create, delete, describe, update.
  -h, --help          help for kubernetes
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
* [yba provider kubernetes create](yba_provider_kubernetes_create.md)	 - Create a Kubernetes YugabyteDB Anywhere provider
* [yba provider kubernetes delete](yba_provider_kubernetes_delete.md)	 - Delete a Kubernetes YugabyteDB Anywhere provider
* [yba provider kubernetes describe](yba_provider_kubernetes_describe.md)	 - Describe a Kubernetes YugabyteDB Anywhere provider
* [yba provider kubernetes list](yba_provider_kubernetes_list.md)	 - List Kubernetes YugabyteDB Anywhere providers
* [yba provider kubernetes update](yba_provider_kubernetes_update.md)	 - Update a Kubernetes YugabyteDB Anywhere provider

