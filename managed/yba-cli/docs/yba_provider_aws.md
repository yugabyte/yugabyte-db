## yba provider aws

Manage a YugabyteDB Anywhere AWS provider

### Synopsis

Manage an AWS provider in YugabyteDB Anywhere

```
yba provider aws [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the provider for the action. Required for create, delete, describe, update.
  -h, --help          help for aws
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
* [yba provider aws create](yba_provider_aws_create.md)	 - Create an AWS YugabyteDB Anywhere provider
* [yba provider aws delete](yba_provider_aws_delete.md)	 - Delete an AWS YugabyteDB Anywhere provider
* [yba provider aws describe](yba_provider_aws_describe.md)	 - Describe an AWS YugabyteDB Anywhere provider
* [yba provider aws list](yba_provider_aws_list.md)	 - List AWS YugabyteDB Anywhere providers
* [yba provider aws update](yba_provider_aws_update.md)	 - Update an AWS YugabyteDB Anywhere provider

