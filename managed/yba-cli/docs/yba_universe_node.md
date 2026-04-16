## yba universe node

Manage YugabyteDB Anywhere universe nodes

### Synopsis

Manage YugabyteDB Anywhere universe nodes. Operations allowed for AWS, Azure, GCP and On-premises universes.

```
yba universe node [flags]
```

### Options

```
  -n, --name string        [Required] The name of the universe for the corresponding node operations.
      --node-name string   [Optional] The name of the universe node for the corresponding node operations. Required for add, reboot, release, remove, reprovision, start, stop.
  -h, --help               help for node
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe](yba_universe.md)	 - Manage YugabyteDB Anywhere universes
* [yba universe node decommission](yba_universe_node_decommission.md)	 - Decommission a node in YugabyteDB Anywhere universe
* [yba universe node hard-reboot](yba_universe_node_hard-reboot.md)	 - Hard reboot a node in YugabyteDB Anywhere universe
* [yba universe node list](yba_universe_node_list.md)	 - List YugabyteDB Anywhere universe nodes
* [yba universe node reboot](yba_universe_node_reboot.md)	 - Reboot a node in YugabyteDB Anywhere universe
* [yba universe node replace](yba_universe_node_replace.md)	 - Decommission and replace a node in a universe with another new node
* [yba universe node reprovision](yba_universe_node_reprovision.md)	 - Reprovision a node in YugabyteDB Anywhere universe
* [yba universe node start-master](yba_universe_node_start-master.md)	 - Start master of a node in YugabyteDB Anywhere universe
* [yba universe node start-processes](yba_universe_node_start-processes.md)	 - Start YugbayteDB processes on a node in YugabyteDB Anywhere universe
* [yba universe node stop-processes](yba_universe_node_stop-processes.md)	 - Stop YugbayteDB processes on a node in YugabyteDB Anywhere universe

