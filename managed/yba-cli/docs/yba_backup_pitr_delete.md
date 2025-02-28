## yba backup pitr delete

Delete the PITR configuration for the universe

### Synopsis

Delete the Point-In-Time Recovery (PITR) configuration for the universe

```
yba backup pitr delete [flags]
```

### Examples

```
yba backup pitr delete --universe-name <universe-name> --uuid <pitr-uuid>
```

### Options

```
  -u, --uuid string   [Required] The UUID of the PITR config to be deleted.
  -h, --help          help for delete
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

