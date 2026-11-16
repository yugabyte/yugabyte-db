## yba universe support-bundlev2

Support bundle v2 operations

### Synopsis

Support bundle v2 operations using the v2 API. Create and estimate bundles from a YAML config; list, download, and delete bundles created through this flow. Global-level legacy components (YbaMetadata, TabletReport, PrometheusMetrics, PerfAdvisor, K8sInfo) are listed as sibling groups under spec.GlobalComponents. Script-driven YBA host components (e.g. ApplicationLogs) live under spec.GlobalComponents.YBA and use YBAComponent specs. Node-level components are mapped to FilesComponent, BashComponent, YSQLComponent, YCQLComponent, or YbAdminComponent specs.

```
yba universe support-bundlev2 [flags]
```

### Options

```
  -n, --name string        [Optional] The name of the universe. Omit when operating on YBA-only support bundles (e.g. ApplicationLogs) on the YBA platform host.
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -s, --skip-validations   [Optional] Skip the universe lookup performed before the command runs. The universe is still resolved when the command needs it, so this only forgoes failing early.
  -h, --help               help for support-bundlev2
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
* [yba universe support-bundlev2 create](yba_universe_support-bundlev2_create.md)	 - Create a support bundle via the v2 API
* [yba universe support-bundlev2 delete](yba_universe_support-bundlev2_delete.md)	 - Delete a support bundle created via the v2 API
* [yba universe support-bundlev2 download](yba_universe_support-bundlev2_download.md)	 - Download a support bundle created via the v2 API
* [yba universe support-bundlev2 estimate](yba_universe_support-bundlev2_estimate.md)	 - Estimate support bundle size via the v2 API
* [yba universe support-bundlev2 list](yba_universe_support-bundlev2_list.md)	 - List support bundles created via the v2 API

