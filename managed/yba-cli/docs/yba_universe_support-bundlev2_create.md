## yba universe support-bundlev2 create

Create a support bundle via the v2 API

### Synopsis

Collect support-bundle components using a YAML config consumed by this CLI.

```
yba universe support-bundlev2 create [flags]
```

### Examples

```
yba universe support-bundlev2 create --name <universe-name>

yba universe support-bundlev2 create --components ApplicationLogs --force

yba universe support-bundlev2 create --name <universe-name> \
  --node-names yb-15-universe-n1,yb-15-universe-n2

yba universe support-bundlev2 create --name <universe-name> \
  --manifest-file devops/bin/support_bundlev2_manifest.yaml \
  --components UniverseLogs,ApplicationLogs --dry-run
```

### Options

```
      --manifest-file string   [Optional] Path to the support bundle v2 manifest yaml. Defaults to bin/support_bundlev2_manifest.yaml under the devops directory, located using the DEVOPS_HOME environment variable or the yb.devops.home key of the YBA application.conf. It selects the components. YBA rejects a component its own manifest lacks or whose script or entrypoint differs, and takes each component's arguments, queries, timeout, tar path and Linux user from its own manifest.
      --components string      [Optional] Comma-separated list of components or component groups to execute (overrides the component selection in yaml spec.NodeComponents and GlobalComponents). A component that the manifest marks enabled: false stays excluded and is reported.
      --start-time string      [Optional] Collection window start, strict RFC3339 (2006-01-02T15:04:05Z) as on support-bundle. Defaults to 24 hours before --end-time or now.
      --end-time string        [Optional] Collection window end, strict RFC3339 (2006-01-02T15:04:05Z), must not be in the future. Defaults to the current time.
      --node-names string      [Optional] Comma-separated list of universe node names to collect node-level components from (overrides yaml spec.nodeSelection.nodeNames). Defaults to all nodes in the universe. Global components such as PrometheusMetrics, YbaMetadata, TabletReport, PerfAdvisor and ApplicationLogs are collected once for the universe and are not scoped by this list. Not applicable to YBA-only support bundles.
      --dry-run                [Optional] Print the generated support bundle request without calling the API.
  -h, --help                   help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the universe. Omit when operating on YBA-only support bundles (e.g. ApplicationLogs) on the YBA platform host.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip the universe lookup performed before the command runs. The universe is still resolved when the command needs it, so this only forgoes failing early.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe support-bundlev2](yba_universe_support-bundlev2.md)	 - Support bundle v2 operations

