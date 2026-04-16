## yba ear aws

Manage a YugabyteDB Anywhere AWS encryption at rest (EAR) configuration

### Synopsis

Manage an AWS encryption at rest (EAR) configuration in YugabyteDB Anywhere

```
yba ear aws [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the configuration for the action. Required for create, delete, describe, update and refresh.
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

* [yba ear](yba_ear.md)	 - Manage YugabyteDB Anywhere Encryption at Rest Configurations
* [yba ear aws create](yba_ear_aws_create.md)	 - Create a YugabyteDB Anywhere AWS encryption at rest configuration
* [yba ear aws delete](yba_ear_aws_delete.md)	 - Delete a YugabyteDB Anywhere AWS encryption at rest configuration
* [yba ear aws describe](yba_ear_aws_describe.md)	 - Describe an AWS YugabyteDB Anywhere Encryption In Transit (EAR) configuration
* [yba ear aws list](yba_ear_aws_list.md)	 - List AWS YugabyteDB Anywhere Encryption In Transit (EAR) configurations
* [yba ear aws refresh](yba_ear_aws_refresh.md)	 - Refresh an AWS YugabyteDB Anywhere Encryption In Transit (EAR) configuration
* [yba ear aws update](yba_ear_aws_update.md)	 - Update a YugabyteDB Anywhere AWS encryption at rest (EAR) configuration

