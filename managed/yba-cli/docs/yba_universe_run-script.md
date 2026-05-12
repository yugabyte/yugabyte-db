## yba universe run-script

Execute a script on database nodes in a universe

### Synopsis

Execute a bash script on one or more database nodes in a universe. The script can be provided inline via --script-content or as a file path on the YBA node via --script-file. Results including stdout, stderr, and exit codes are returned for each node.

```
yba universe run-script [flags]
```

### Examples

```
yba universe run-script --name <universe-name> \
  --script-content "df -h && free -m"

yba universe run-script --name <universe-name> \
  --script-content "df -h" --node-names "yb-1-node-n1,yb-1-node-n2"

yba universe run-script --name <universe-name> \
  --script-file /tmp/diagnostics.sh --timeout-secs 120

yba universe run-script --name <universe-name> \
  --script-content "cat /proc/cpuinfo" --masters-only

yba universe run-script --name <universe-name> \
  --script-file /tmp/check.sh --params "arg1,arg2,arg3"
```

### Options

```
  -n, --name string             [Required] The name of the universe to run the script on.
  -f, --force                   [Optional] Bypass the prompt for non-interactive usage.
      --skip-validations        [Optional] Skip validations before running the script. [default: false]
      --script-content string   [Optional*] Inline script content to execute on nodes. [33mMutually exclusive with --script-file.
      --script-file string      [Optional*] Path to a script file on the YBA node. [33mMutually exclusive with --script-content.
      --params string           [Optional] Comma-separated command-line arguments to pass to the script.
      --timeout-secs int        [Optional] Timeout in seconds for script execution per node (default 60, max 3600). (default 60)
      --linux-user string       [Optional] Linux user to run the script as. (default "yugabyte")
      --node-names string       [Optional] Comma-separated list of specific node names to target.
      --masters-only            [Optional] Run the script only on master nodes. (default: false)
      --tservers-only           [Optional] Run the script only on tserver nodes. (default: false)
  -h, --help                    help for run-script
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

