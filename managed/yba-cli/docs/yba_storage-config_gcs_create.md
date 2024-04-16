## yba storage-config gcs create

Create a GCS YugabyteDB Anywhere storage configuration

### Synopsis

Create a GCS storage configuration in YugabyteDB Anywhere

```
yba storage-config gcs create [flags]
```

### Options

```
      --backup-location string         [Required] The complete backup location including "gs://".
      --credentials-file-path string   GCS Credentials File Path. Required for non IAM role based storage configurations. Can also be set using environment variable GOOGLE_APPLICATION_CREDENTIALS.
      --use-gcp-iam                    [Optional] Use IAM Role from the YugabyteDB Anywhere Host. Supported for Kubernetes GKE clusters with workload identity. Configuration creation will fail on insufficient permissions on the host. (default false)
  -h, --help                           help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the storage configuration for the operation. Required for create, delete, describe, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba storage-config gcs](yba_storage-config_gcs.md)	 - Manage a YugabyteDB Anywhere GCS storage configuration

