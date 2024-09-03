## yba ear gcp create

Create a YugabyteDB Anywhere GCP encryption at rest configuration

### Synopsis

Create a GCP encryption at rest configuration in YugabyteDB Anywhere

```
yba ear gcp create [flags]
```

### Options

```
      --credentials-file-path string   GCP Credentials File Path. Can also be set using environment variable GOOGLE_APPLICATION_CREDENTIALS.
      --location string                [Optional] The geographical region where the Cloud KMS resource is stored and accessed. (default "global")
      --key-ring-name string           [Required] Name of the key ring. If key ring with same name already exists then it will be used, else a new one will be created automatically.
      --crypto-key-name string         [Required] Name of the cryptographic key that will be used for encrypting and decrypting universe key. If crypto key with same name already exists then it will be used, else a new one will be created automatically.
      --protection-level string        [Optional] The protection level to use for this key. Allowed values (case sensitive): SOFTWARE and HSM. (default "HSM")
      --endpoint string                [Optional] GCP KMS Endpoint.
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
  -n, --name string        [Optional] The name of the configuration for the action. Required for create, delete, describe, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba ear gcp](yba_ear_gcp.md)	 - Manage a YugabyteDB Anywhere GCP encryption at rest (EAR) configuration

