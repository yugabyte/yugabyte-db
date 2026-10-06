## yba perf-advisor endpoint update

Update a YugabyteDB Anywhere Perf Advisor endpoint

### Synopsis

Update an external Perf Advisor destination. The new settings are probed before they are stored, then pushed to every collector already using this endpoint, so universes already forwarding here pick them up without waiting for the next sync.

```
yba perf-advisor endpoint update [flags]
```

### Examples

```
yba perf-advisor endpoint update --name byoc-prod --password <new-password>
```

### Options

```
  -n, --name string                  [Required] Name of the Perf Advisor endpoint.
      --type string                  [Optional] Endpoint kind, case-insensitive. Allowed values: BYOC. (default "BYOC")
      --collection-endpoint string   [Required] URL of the destination's Collection API.
      --metrics-endpoint string      [Required] URL metrics are sent to.
      --metrics-type string          [Optional] Metrics protocol, case-insensitive. Allowed values: otlphttp, remotewrite. (default "otlphttp")
      --auth-type string             [Optional] Authentication for both endpoints, case-insensitive. Allowed values: NONE, BASIC. (default "NONE")
      --username string              [Optional] Username for both endpoints. Required for BASIC.
      --password string              [Optional] Password for both endpoints. Required for BASIC.
      --ybm-account-id string        [Optional] YugabyteDB Managed account ID, sent as the YBM-Account-ID header. Required by a BYOC ingest gateway.
      --ybm-project-id string        [Optional] YugabyteDB Managed project ID, sent as the YBM-Project-ID header.
  -h, --help                         help for update
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

* [yba perf-advisor endpoint](yba_perf-advisor_endpoint.md)	 - Manage YugabyteDB Anywhere Perf Advisor endpoints

