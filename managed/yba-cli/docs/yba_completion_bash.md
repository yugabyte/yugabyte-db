## yba completion bash

Generate the autocompletion script for bash

### Synopsis

Generate the autocompletion script for the bash shell.

This script depends on the 'bash-completion' package.
If it is not installed already, you can install it via your OS's package manager.

To load completions in your current shell session:

	source <(yba completion bash)

To load completions for every new session, execute once:

#### Linux:

	yba completion bash > /etc/bash_completion.d/yba

#### macOS:

	yba completion bash > $(brew --prefix)/etc/bash_completion.d/yba

You will need to start a new shell for this setup to take effect.


```
yba completion bash
```

### Options

```
  -h, --help              help for bash
      --no-descriptions   disable completion descriptions
```

### Options inherited from parent commands

```
  -a, --apiKey string      YugabyteDB Anywhere api key
      --config string      config file, default is $HOME/.yba-cli.yaml
      --debug              use debug mode, same as --logLevel debug
  -H, --host string        YugabyteDB Anywhere Host, default to http://localhost:9000
  -l, --logLevel string    select the desired log level format, default to info
      --no-color           disable colors in output , default to false
  -o, --output string      select the desired output format (table, json, pretty), default to table
      --timeout duration   wait command timeout,example: 5m, 1h. (default 168h0m0s)
      --wait               wait until the task is completed, otherwise it will exit immediately, default to true
```

### SEE ALSO

* [yba completion](yba_completion.md)	 - Generate the autocompletion script for the specified shell

