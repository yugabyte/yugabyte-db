## yba auth

Authenticate yba cli

### Synopsis

Authenticate the yba cli through this command by providing the host and API Token. If non-interactive mode is set, provide the host and API Token using flags. Default for host is "http://localhost:9000"

```
yba auth [flags]
```

### Examples

```
yba auth -f -H <host> -a <api-token>
```

### Options

```
  -f, --force   [Optional] Bypass the prompt for non-interactive usage. Provide the host (--host/-H) and API token (--apiToken/-a) using flags
  -h, --help    help for auth
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

