## yba backup pitr recover

Recover universe keyspace to a point in time

### Synopsis

Recover universe keyspace to a point in time

```
yba backup pitr recover [flags]
```

### Examples

```
yba backup pitr recover --universe-name <universe-name> --uuid <pitr-uuid>
	 --timestamp <timestamp>
```

### Options

```
  -u, --uuid string     [Required] The UUID of the PITR config.
  -t, --timestamp int   [Required] Unix epoch timestamp in milliseconds to which the universe keyspace needs to be recovered.
  -h, --help            help for recover
```

### Options inherited from parent commands

```
  -a, --apiToken string        YugabyteDB Anywhere api token.
      --config string          Config file, defaults to $HOME/.yba-cli.yaml
      --debug                  Use debug mode, same as --logLevel debug.
      --disable-color          Disable colors in output. (default false)
  -H, --host string            YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string        Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string          Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration       Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --universe-name string   [Required] The name of the universe associated with the PITR configuration.
      --wait                   Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba backup pitr](yba_backup_pitr.md)	 - Manage YugabyteDB Anywhere universe PITR configs

