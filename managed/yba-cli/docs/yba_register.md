## yba register

Register a YugabyteDB Anywhere customer using yba cli

### Synopsis

Register a YugabyteDB Anywhere customer using yba cli. If non-interactive mode is set, provide the host, name, email, password and environment using flags.

```
yba register [flags]
```

### Examples

```
yba register -f -n <name> -e <email> -p <password> -H <host>
```

### Options

```
  -n, --name string          [Optional] Name of the user. Required for non-interactive usage
  -e, --email string         [Optional] Email for the user. Required for non-interactive usage
  -p, --password string      [Optional] Password for the user. Password must contain at least 8 characters and at least 1 digit , 1 capital , 1 lowercase and 1 of the !@#$^&* (special) characters. Required for non-interactive usage. Use single quotes ('') to provide values with special characters.
      --environment string   [Optional] Environment of the installation. Allowed values: dev, demo, stage, prod. (default "dev")
  -f, --force                [Optional] Bypass the prompt for non-interactive usage.
  -h, --help                 help for register
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

