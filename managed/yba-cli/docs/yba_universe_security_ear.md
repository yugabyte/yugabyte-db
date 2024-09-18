## yba universe security ear

Encryption-at-rest settings for a universe

### Synopsis

Encryption-at-rest settings for a universe

```
yba universe security ear [flags]
```

### Options

```
      --operation string     [Required] Enable or disable encryption-at-rest in a universe. Allowed values: enable, disable, rotate-universe-key, rotate-kms-config.
      --config-name string   [Optional] Key management service configuration name for master key. Required for enable and rotate-kms-config operations, ignored otherwise.
  -h, --help                 help for ear
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Required] The name of the universe for the operation.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe security](yba_universe_security.md)	 - Manage security settings for a universe

