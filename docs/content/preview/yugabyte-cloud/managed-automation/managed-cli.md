---
title: ybm CLI
headerTitle: ybm CLI
linkTitle: ybm CLI
description: Use YugabyteDB Managed CLI to access YugabyteDB clusters.
headcontent: Manage cluster and account resources from the command line
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli
    parent: managed-automation
    weight: 20
type: docs
rightNav:
  hideH4: true
---

The [YugabyteDB Managed Command Line Interface](https://github.com/yugabyte/ybm-cli) (ybm) is an open source tool that enables you to interact with YugabyteDB Managed accounts using commands in your command-line shell. With minimal configuration, the CLI enables you to start running commands that implement functionality equivalent to that provided by the browser-based YugabyteDB Managed interface from the command prompt in your shell.

You can install ybm CLI using Homebrew:

```sh
brew install yugabyte/yugabytedb/ybm
```

If you don't have a YugabyteDB Managed account yet, you can use the following command to bring up the sign up page:

```sh
ybm signup
```

## Configure ybm

Using ybm CLI requires providing, at minimum, an [API key](../managed-apikeys/) and the host address of your cluster.

You can pass these values as flags when running ybm commands. For example:

```sh
ybm --apikey "eyJ..." --host cloud.yugabyte.com cluster list
```

For convenience, you can configure ybm with default values for these flags as follows:

- Use the `auth` command to write these values to a YAML configuration file. For example:

  ```sh
  ybm auth --apikey "eyJ..."
  ```

  By default, this writes the values to the file `.ybm-cli.yaml` under your `$HOME` directory.

  You can create multiple configuration files, and switch between them using the [--config](#global-flags) flag.

- Using [environment variables](#environment-variables). Environment variables must begin with `YBM_`. For example:

  ```sh
  export YBM_APIKEY=AWERDFSSS
  ybm cluster list
  ```

### Environment variables

In the case of multi-node deployments, all nodes should have similar environment variables.

You can set the following environment variables:

- `YBM_APIKEY`

  The API key to use to authenticate to your YugabyteDB Managed account.

- `YBM_HOST`

  The host address of the cluster you are managing. By default, https is added to the host if no scheme is provided.

- `YBM_CI`

  Set to `true` to avoid outputting unnecessary log lines.

### Autocompletion

You can configure command autocompletion for your shell using the `completion` command. For example:

```sh
ybm completion bash
```

This generates an autocompletion script for the specified shell. Available options are as follows:

- bash
- fish
- powershell
- zsh

#### Bash

ybm CLI autocompletion depends on the 'bash-completion' package. If not already installed, install it using your operating system's package manager.

To load completions in your current shell session, enter the following command:

```sh
source <(ybm completion bash)
```

To load completions for every new session, execute the following command:

```sh
#### for Linux:
ybm completion bash > /etc/bash_completion.d/ybm

#### for macOS:
ybm completion bash > $(brew --prefix)/etc/bash_completion.d/ybm
```

Start a new shell for the setup to take effect.

#### fish

To load completions in your current shell session, use the following command:

```sh
ybm completion fish | source
```

To load completions for every new session, execute the following command:

```sh
ybm completion fish > ~/.config/fish/completions/ybm.fish
```

Start a new shell for the setup to take effect.

#### PowerShell

To load completions in your current shell session, use the following command:

```sh
ybm completion powershell | Out-String | Invoke-Expression
```

To load completions for every new session, add the output of the preceding command to your PowerShell profile.

#### Zsh

If shell completion is not already enabled in your environment, you can turn it on by running the following command:

```sh
echo "autoload -U compinit; compinit" >> .zshrc
```

To load completions in your current shell session, enter the following command:

```sh
source <(ybm completion zsh); compdef _ybm ybm
```

To load completions for every new session, execute the following:

```sh
#### for Linux:
ybm completion zsh > "${fpath[1]}/_ybm"

#### for macOS:
ybm completion zsh > $(brew --prefix)/share/zsh/site-functions/_ybm
```

Start a new shell for the setup to take effect.

## Syntax

```sh
ybm [-h] [ <resource> ] [ <command> ] [ <flags> ]
```

- resource: resource to be changed
- command: command to run
- flags: one or more flags, separated by spaces.

For example:

```sh
ybm cluster create
```

### Online help

You can access command-line help for ybm by running the following commands from YugabyteDB home:

```sh
ybm -h
```

```sh
ybm --help
```

For help with specific ybm resource commands, run `ybm [ resource ] [ command ] -h`. For example, you can print the command-line help for the `ybm create` command by running the following:

```sh
ybm cluster read-replica create -h
```

### Global flags

The following flags can be passed in with any command:

-a, --apiKey string
: YugabyteDB Managed account API Key.

--config string
: Configuration file (default is $HOME/.ybm-cli.yaml).

--debug
: Use debug mode, same as `--logLevel debug`.

--host string
: Host address of the cluster.

-l, --logLevel string
: Specify the desired level of logging. `debug` or `info` (default).

--no-color
: Disable colors in output. `true` or `false` (default).

-o, --output string
: Specify the desired output format. `table` (default), `json`, or `pretty`.

--wait
: For long-running commands such as creating or deleting a cluster, you can use the `--wait` flag to wait until the operation is completed. For example:

```sh
ybm cluster delete \
    --cluster-name=test-cluster \
    --wait
```

If you are using ybm with the `--wait` flag in your CI system, you can set the environment variable `YBM_CI` to `true` to avoid generating unnecessary log lines.

## Resources

You can manage the following resources using the CLI:

- [backup](../managed-cli-backup/)
- [cluster](managed-cli-cluster/)
  - [network](managed-cli-network/)
  - [read-replica](managed-cli-read-replica/)
- [network-allow-list](managed-cli-network-allow-list/)
- [vpc](managed-cli-vpc/)
  - [peering](managed-cli-peering/)
- [region](managed-cli-region/)

<!--
- [cdc-sink](managed-cli-cdc-sink/)
- [cdc-stream](managed-cli-cdc-stream/) -->
