## yba ha instance promote-instance

Promote the local standby to leader

### Synopsis

Promote the local platform instance from standby to leader using a backup from the current leader

```
yba ha instance promote-instance [flags]
```

### Examples

```
yba ha instance promote-instance --uuid <uuid> --instance-uuid <instance-uuid> --backup-file <filename> [--current-leader <instance-ip>] [--force]
```

### Options

```
      --uuid string             [Required] UUID of the HA configuration
      --instance-uuid string    [Required] UUID of the local instance to promote
      --backup-file string      [Required] Backup file name from the current leader
      --current-leader string   [Optional] IP address of instance (current leader) (default: resolved from config)
  -f, --force                   [Optional] Skip connection check to current leader (default: true) (default true)
  -h, --help                    help for promote-instance
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

