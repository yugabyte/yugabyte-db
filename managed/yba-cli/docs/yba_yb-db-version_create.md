## yba yb-db-version create

Create a YugabyteDB version entry on YugabyteDB Anywhere

### Synopsis

Create a YugabyteDB version entry on YugabyteDB Anywhere. Run this command after the information provided in the "yba yb-db-version artifact-create <url/upload>" commands.

```
yba yb-db-version create [flags]
```

### Examples

```
yba yb-db-version create --version <version> --type PREVIEW --platform LINUX
	--arch x86_64 --yb-type YBDB --url <url>
```

### Options

```
  -v, --version string    [Required] YugabyteDB version to be created
      --type string       [Required] Release type. Allowed values: lts, sts, preview
      --platform string   [Optional] Platform supported by this version. Allowed values: linux, kubernetes (default "LINUX")
      --arch string       [Optional] Architecture supported by this version. Required if platform is LINUX. Allowed values: x86_64, aarch64
      --yb-type string    [Optional] Type of the release. Allowed values: YBDB (default "YBDB")
      --file-id string    [Optional] File ID of the release tgz file to be used. This is the metadata UUID from the "yba yb-db-version artifact-create upload" command. Provide either file-id or url.
      --url string        [Optional] URL to extract release metadata from a remote tarball. Provide either file-id or url.
      --sha256 string     [Optional] SHA256 of the release tgz file. Required if file-id is provided.
      --date-msecs int    [Optional] Date in milliseconds since the epoch when the release was created.
      --notes string      [Optional] Release notes.
      --tag string        [Optional] Release tag.
  -h, --help              help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba yb-db-version](yba_yb-db-version.md)	 - Manage YugabyteDB versions

