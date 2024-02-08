## yba

yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.

### Synopsis


	YugabyteDB Anywhere is a control plane for managing YugabyteDB universes
	across hybrid and multi-cloud environments, and provides automation and
	orchestration capabilities. YugabyteDB Anywhere CLI provides ease of access
	via the command line.

```
yba [flags]
```

### Options

```
  -a, --apiKey string      YugabyteDB Anywhere api key
      --config string      config file, default is $HOME/.yba-cli.yaml
      --debug              use debug mode, same as --logLevel debug
  -h, --help               help for yba
  -H, --host string        YugabyteDB Anywhere Host, default to http://localhost:9000
  -l, --logLevel string    select the desired log level format, default to info
      --no-color           disable colors in output , default to false
  -o, --output string      select the desired output format (table, json, pretty), default to table
      --timeout duration   wait command timeout,example: 5m, 1h. (default 168h0m0s)
      --wait               wait until the task is completed, otherwise it will exit immediately, default to true
```

### SEE ALSO

* [yba auth](yba_auth.md)	 - Authenticate yba cli
* [yba completion](yba_completion.md)	 - Generate the autocompletion script for the specified shell
* [yba provider](yba_provider.md)	 - Manage YugabyteDB Anywhere providers

