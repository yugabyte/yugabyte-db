## yba storage-config s3 update

Update an S3 YugabyteDB Anywhere storage configuration

### Synopsis

Update an S3 storage configuration in YugabyteDB Anywhere

```
yba storage-config s3 update [flags]
```

### Options

```
      --update-credentials         [Optional] Update credentials of the storage configuration. (default false) If set to true, provide either (access-key-id,secret-access-key) pair or set use-iam-instance-profile.
      --access-key-id string       S3 Access Key ID. Required for non IAM role based storage configurations. Can also be set using environment variable AWS_ACCESS_KEY_ID.
      --secret-access-key string   S3 Secret Access Key. Required for non IAM role based storage configurations. Can also be set using environment variable AWS_SECRET_ACCESS_KEY.
      --new-name string            [Optional] Update name of the storage configuration.
      --use-iam-instance-profile   [Optional] Use IAM Role from the YugabyteDB Anywhere Host. Configuration creation will fail on insufficient permissions on the host. (default false)
  -h, --help                       help for update
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

* [yba storage-config s3](yba_storage-config_s3.md)	 - Manage a YugabyteDB Anywhere S3 storage configuration

