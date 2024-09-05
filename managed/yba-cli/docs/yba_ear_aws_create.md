## yba ear aws create

Create a YugabyteDB Anywhere AWS encryption at rest configuration

### Synopsis

Create an AWS encryption at rest configuration in YugabyteDB Anywhere

```
yba ear aws create [flags]
```

### Options

```
      --access-key-id string          AWS Access Key ID. Required for non IAM role based configurations. Can also be set using environment variable AWS_ACCESS_KEY_ID.
      --secret-access-key string      AWS Secret Access Key. Required for non IAM role based configurations. Can also be set using environment variable AWS_SECRET_ACCESS_KEY.
      --region string                 AWS region where the customer master key is located. Can also be set using environment variable AWS_REGION
      --use-iam-instance-profile      [Optional] Use IAM Role from the YugabyteDB Anywhere Host. EAR creation will fail on insufficient permissions on the host. (default false)
      --cmk-id string                 [Optional] Customer Master Key ID. If an identifier is not entered, a CMK ID will be auto-generated.
      --endpoint string               [Optional] AWS KMS Endpoint.
      --cmk-policy-file-path string   [Optional] AWS KMS Customer Master Key Policy file path. Custom policy file is not needed when Customer Master Key ID is specified. Allowed file type is json.
  -h, --help                          help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the configuration for the action. Required for create, delete, describe, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba ear aws](yba_ear_aws.md)	 - Manage a YugabyteDB Anywhere AWS encryption at rest (EAR) configuration

