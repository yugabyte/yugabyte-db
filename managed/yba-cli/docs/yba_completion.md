## yba completion

Generate the autocompletion script for the specified shell

### Synopsis

Generate the autocompletion script for yba for the specified shell.
See each sub-command's help for details on how to use the generated script.


### Options

```
  -h, --help   help for completion
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

* [yba](yba.md)	 - yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.
* [yba completion bash](yba_completion_bash.md)	 - Generate the autocompletion script for bash
* [yba completion fish](yba_completion_fish.md)	 - Generate the autocompletion script for fish
* [yba completion powershell](yba_completion_powershell.md)	 - Generate the autocompletion script for powershell
* [yba completion zsh](yba_completion_zsh.md)	 - Generate the autocompletion script for zsh

