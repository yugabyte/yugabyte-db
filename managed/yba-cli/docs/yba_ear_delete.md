## yba ear delete

Delete a YugabyteDB Anywhere encryption at rest configuration

### Synopsis

Delete an encryption at rest configuration in YugabyteDB Anywhere

```
yba ear delete [flags]
```

### Options

```
  -n, --name string   [Required] The name of the configuration to be deleted.
  -c, --code string   [Optional] Code of the configuration. Allowed values: aws, gcp, azu, hashicorp.
  -f, --force         [Optional] Bypass the prompt for non-interactive usage.
  -h, --help          help for delete
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

* [yba ear](yba_ear.md)	 - Manage YugabyteDB Anywhere Encryption at Rest Configurations

