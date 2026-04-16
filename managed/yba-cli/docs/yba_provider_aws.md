## yba provider aws

Manage a YugabyteDB Anywhere AWS provider

### Synopsis

Manage an AWS provider in YugabyteDB Anywhere

```
yba provider aws [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the provider for the action. Required for create, delete, describe, update, and some instance-type subcommands.
  -h, --help          help for aws
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
* [yba provider aws create](yba_provider_aws_create.md)	 - Create an AWS YugabyteDB Anywhere provider
* [yba provider aws delete](yba_provider_aws_delete.md)	 - Delete an AWS YugabyteDB Anywhere provider
* [yba provider aws describe](yba_provider_aws_describe.md)	 - Describe an AWS YugabyteDB Anywhere provider
* [yba provider aws instance-type](yba_provider_aws_instance-type.md)	 - Manage YugabyteDB Anywhere AWS instance types
* [yba provider aws list](yba_provider_aws_list.md)	 - List AWS YugabyteDB Anywhere providers
* [yba provider aws update](yba_provider_aws_update.md)	 - Update an AWS YugabyteDB Anywhere provider

