## yba ha instance create

Create an HA platform instance

### Synopsis

Create a local or remote platform instance for an HA configuration. Run this command after the information provided in the "yba ha create" commands.

```
yba ha instance create [flags]
```

### Examples

```
yba ha instance create --uuid <uuid> --ip <instance-ip> --is-leader --is-local
```

### Options

```
      --uuid string   [Required] UUID of the HA configuration
      --ip string     [Required] IP address of instance (e.g., https://<ip-address or hostname>:<port>)
      --is-leader     [Optional] Whether this instance is the leader
      --is-local      [Optional] Whether this is the local instance
  -h, --help          help for create
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

* [yba ha instance](yba_ha_instance.md)	 - Manage HA instances

