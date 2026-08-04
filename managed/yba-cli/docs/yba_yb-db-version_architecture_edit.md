## yba yb-db-version architecture edit

Edit architectures to a version of YugabyteDB

### Synopsis

Edit architectures for a version of YugabyteDB. Run this command after the information provided in the "yba yb-db-version artifact-create <url/upload>" commands.

```
yba yb-db-version architecture edit [flags]
```

### Examples

```
yba yb-db-version architecture edit  --version <version> \
	--platform <platform> --arch <architecture> --sha256 <sha256>
```

### Options

```
      --platform string   [Required] Platform supported by this version. Allowed values: linux, kubernetes
      --arch string       [Optional] Architecture supported by this version. Required if platform is LINUX. Allowed values: x86_64, aarch64
      --sha256 string     [Optional] SHA256 of the release tgz file.
  -h, --help              help for edit
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
  -v, --version string     [Required] YugabyteDB version to be updated.
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba yb-db-version architecture](yba_yb-db-version_architecture.md)	 - Manage architectures for a version of YugabyteDB

