## yba ear ciphertrust create

Create a YugabyteDB Anywhere CipherTrust encryption at rest configuration

### Synopsis

Create a CipherTrust encryption at rest configuration in YugabyteDB Anywhere

```
yba ear ciphertrust create [flags]
```

### Examples

```
yba ear ciphertrust create --name <config-name> \
    --manager-url <ciphertrust-manager-url> --auth-type <PASSWORD|REFRESH_TOKEN> \
    [--username <username> --password <password> | --refresh-token <token>] \
    --key-name <key-name> --key-algorithm AES --key-size <128|192|256>
```

### Options

```
      --manager-url string     [Required] CipherTrust Manager URL.
      --auth-type string       [Optional]Authentication type. Allowed values (case sensitive): PASSWORD, REFRESH_TOKEN (default "PASSWORD")
      --username string        [Optional] CipherTrust username (for auth-type PASSWORD)
      --password string        [Optional] CipherTrust password (for auth-type PASSWORD)
      --refresh-token string   [Optional] CipherTrust refresh token (for auth-type REFRESH_TOKEN)
      --key-name string        [Required] CipherTrust key name
      --key-algorithm string   [Optional] CipherTrust key algorithm. Allowed values (case sensitive): AES (default "AES")
      --key-size int           [Optional] CipherTrust key size for algorithm AES. Allowed values: 128, 192, 256 (default 256)
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
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the configuration for the action. Required for create, delete, describe, update and refresh.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba ear ciphertrust](yba_ear_ciphertrust.md)	 - Manage a YugabyteDB Anywhere CipherTrust encryption at rest (EAR) configuration

