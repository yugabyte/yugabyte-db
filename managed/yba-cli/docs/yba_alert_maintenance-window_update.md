## yba alert maintenance-window update

Update a maintenance window to suppress alerts

### Synopsis

Update a maintenance window to suppress alerts

```
yba alert maintenance-window update [flags]
```

### Options

```
  -u, --uuid string                          [Required] UUID of the maintenance window.
      --name string                          [Optional] Update name of the maintenance window.
      --description string                   [Optional] Update description of the maintenance window.
      --start-time string                    [Optional] Update ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for start time of the maintenance window.
      --end-time string                      [Optional] UpdateISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for end time of the maintenance window.
      --alert-target-uuids string            [Optional] Update comma separated list of universe UUIDs to suppress alerts. If left empty, suppress alerts on all the universes (including future universes).
      --health-check-universe-uuids string   [Optional] Update comma separated list of universe UUIDs to suppress health checks. If left empty, suppress health check notifications on all the universes (including future universes).
      --mark-as-complete                     [Optional] Mark the maintenance window as complete.
  -h, --help                                 help for update
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

* [yba alert maintenance-window](yba_alert_maintenance-window.md)	 - Manage YugabyteDB Anywhere maintenance window

