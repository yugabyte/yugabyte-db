## yba login

Authenticate yba cli using email and password

### Synopsis

Connect to YugabyteDB Anywhere host machine using email and password. If non-interactive mode is set, provide the host, email and password using flags. Default for host is "http://localhost:9000"

```
yba login [flags]
```

### Examples

```
yba login -f -e <email> -p <password> -H <host>
```

### Options

```
  -e, --email string      [Optional] Email or username for the user. Required for non-interactive usage.
  -p, --password string   [Optional] Password for the user. Required for non-interactive usage. Use single quotes ('') to provide values with special characters.
  -f, --force             [Optional] Bypass the prompt for non-interactive usage.
  -h, --help              help for login
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

