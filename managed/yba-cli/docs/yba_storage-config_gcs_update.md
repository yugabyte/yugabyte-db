## yba storage-config gcs update

Update an GCS YugabyteDB Anywhere storage configuration

### Synopsis

Update an GCS storage configuration in YugabyteDB Anywhere

```
yba storage-config gcs update [flags]
```

### Options

```
      --update-credentials             [Optional] Update credentials of the storage configuration. (default false) If set to true, provide either credentials-file-path or set use-gcp-iam.
      --credentials-file-path string   GCS Credentials File Path. Required for non IAM role based storage configurations. Can also be set using environment variable GOOGLE_APPLICATION_CREDENTIALS.
      --use-gcp-iam                    [Optional] Use IAM Role from the YugabyteDB Anywhere Host. Supported for Kubernetes GKE clusters with workload identity. Configuration creation will fail on insufficient permissions on the host. (default false)
      --new-name string                [Optional] Update name of the storage configuration.
  -h, --help                           help for update
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

