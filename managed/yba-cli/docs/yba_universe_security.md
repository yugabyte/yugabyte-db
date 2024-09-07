## yba universe security

Manage security settings for a universe

### Synopsis

Manage security settings for a universe

```
yba universe security [flags]
```

### Options

```
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -n, --name string        [Required] The name of the universe for the operation.
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
  -h, --help               help for security
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

* [yba universe](yba_universe.md)	 - Manage YugabyteDB Anywhere universes
* [yba universe security ear](yba_universe_security_ear.md)	 - Encryption-at-rest settings for a universe

