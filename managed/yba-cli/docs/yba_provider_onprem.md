## yba provider onprem

Manage a YugabyteDB Anywhere on-premises provider

### Synopsis

Create and manage an on-premises provider, instance types and node instances in YugabyteDB Anywhere.

```
yba provider onprem [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the provider for the action. Required for create, delete, describe, instance-types and nodes.
  -h, --help          help for onprem
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
* [yba provider onprem create](yba_provider_onprem_create.md)	 - Create an On-premises YugabyteDB Anywhere provider
* [yba provider onprem delete](yba_provider_onprem_delete.md)	 - Delete an On-premises YugabyteDB Anywhere provider
* [yba provider onprem describe](yba_provider_onprem_describe.md)	 - Describe an On-premises YugabyteDB Anywhere provider
* [yba provider onprem instance-types](yba_provider_onprem_instance-types.md)	 - Manage YugabyteDB Anywhere onprem instance types
* [yba provider onprem list](yba_provider_onprem_list.md)	 - List On-premises YugabyteDB Anywhere providers
* [yba provider onprem node](yba_provider_onprem_node.md)	 - Manage YugabyteDB Anywhere onprem node instances
* [yba provider onprem update](yba_provider_onprem_update.md)	 - Update an On-premises YugabyteDB Anywhere provider

