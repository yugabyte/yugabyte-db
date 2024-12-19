## yba universe upgrade gflags set

Set gflags for a YugabyteDB Anywhere Universe

### Synopsis

Set gflags for a YugabyteDB Anywhere Universe. Refer to https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/templates for structure of specific gflags file.

```
yba universe upgrade gflags set [flags]
```

### Examples

```
yba universe upgrade gflags set -n <universe-name> \
	--specific-gflags-file-path <file-path>
```

### Options

```
      --specific-gflags string               [Optional] Specific gflags to be set. Use the modified output of "yba universe upgrade gflags get" command as the flag value. Quote the string with single quotes. Provider either specific-gflags or specific-gflags-file-path
      --specific-gflags-file-path string     [Optional] Path to modified json output file of "yba universe upgrade gflags get" command. Provider either specific-gflags or specific-gflags-file-path
      --upgrade-option string                [Optional] Upgrade Options, defaults to Rolling. Allowed values (case sensitive): Rolling, Non-Rolling (involves DB downtime), Non-Restart (default "Rolling")
      --delay-between-master-servers int32   [Optional] Upgrade delay between Master servers (in miliseconds). (default 18000)
      --delay-between-tservers int32         [Optional] Upgrade delay between Tservers (in miliseconds). (default 18000)
  -h, --help                                 help for set
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
  -n, --name string        [Required] The name of the universe to be upgraded.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe upgrade gflags](yba_universe_upgrade_gflags.md)	 - Gflags upgrade for a YugabyteDB Anywhere Universe

