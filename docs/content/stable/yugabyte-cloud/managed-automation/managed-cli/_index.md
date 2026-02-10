---
title: YugabyteDB Aeon CLI (ybm)
headerTitle: YugabyteDB Aeon CLI
linkTitle: ybm CLI
description: Use YugabyteDB Aeon CLI to create and manage YugabyteDB clusters.
headcontent: Manage cluster and account resources from the command line
menu:
  stable_yugabyte-cloud:
    identifier: managed-cli
    parent: managed-automation
    weight: 50
aliases:
  - /stable/yugabyte-cloud/managed-automation/managed-cli/managed-cli-overview/
type: indexpage
showRightNav: true
rightNav:
  hideH4: true
---

The [YugabyteDB Aeon Command Line Interface](https://github.com/yugabyte/ybm-cli) (ybm) is an open source tool that enables you to interact with YugabyteDB Aeon accounts using commands in your command-line shell. With minimal configuration, you can start running commands from the command line that implement functionality equivalent to that provided by the browser-based YugabyteDB Aeon interface.

{{< youtube id="wAtP7qzYhgU" title="Use the ybm CLI to manage clusters in YugabyteDB Aeon" >}}

## Install ybm

On macOS and Linux, you can also install ybm using [Homebrew](https://brew.sh) by entering the following command:

```sh
brew install yugabyte/tap/ybm
```

If you don't have a YugabyteDB Aeon account yet, you can use the following command to bring up the sign up page:

```sh
ybm signup
```

For help, enter the following command:

```sh
ybm help
```

### Upgrade ybm

To upgrade ybm using Homebrew, enter the following commands:

```sh
brew update && brew upgrade ybm
```

### Configure ybm

Using ybm CLI requires providing, at minimum, an [API key](../managed-apikeys/).

You can pass the key as a [flag](#global-flags) when running ybm commands. For example:

```sh
ybm cluster list --apiKey "eyJ..."
```

For convenience, you can configure ybm with a default API key as follows:

- Use the `auth` command to write the key to a YAML configuration file, as follows:

  ```sh
  ybm auth
  ```

  At the prompt, paste your API key and press Enter.

  By default, this writes the value to the file `.ybm-cli.yaml` under your `$HOME` directory.

  You can create multiple configuration files, and switch between them using the `--config` flag. You can add any of the other [global flags](#global-flags) to your configuration files.

- Using [environment variables](#environment-variables). Environment variables must begin with `YBM_`. For example:

  ```sh
  export YBM_APIKEY=AWERDFSSS
  ybm cluster list
  ```

If a value is set in an environment variable and a configuration file, the environment variable takes precedence. Setting the value using the flag takes precedence over both.

### Environment variables

You can set the following ybm environment variables.

| Variable | Description |
| :--- | :--- |
| YBM_APIKEY | The API key to use to authenticate to your YugabyteDB Aeon account. |
| YBM_CI | Set to `true` to avoid outputting unnecessary log lines. |
| YBM_AWS_SECRET_KEY | AWS secret access key. For encryption at rest of AWS clusters. |
<!--| YBM_HOST | The host address of the cluster you are managing. By default, https is added to the host if no scheme is provided. |-->

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

Access command-line help for ybm by running the following command:

```sh
ybm help
```

For help with specific ybm resource commands, use the `--help` or `-h` flag, in the following form:

```sh
ybm [ <resource> ] [ <command> ] -h
```

For example, to print the command-line help for the cluster `create` command, run the following:

```sh
ybm cluster create -h
```

Print the version of ybm CLI:

```sh
ybm --version
```

### Command reference

Reference documentation for ybm CLI commands and their flags is available in the [Docs folder](https://github.com/yugabyte/ybm-cli/blob/main/docs/ybm.md) of the GitHub repository.

### Global flags

The following flags can be passed in with any command. These flags can also be added to your configuration file (see [Configure ybm](#configure-ybm)).

-a, --apiKey string
: YugabyteDB Aeon account API key.

--config string
: Configuration file (default is `$HOME/.ybm-cli.yaml`).

--debug
: Use debug mode, same as `--logLevel debug`.

-h, --help
: Displays help information for ybm CLI commands.

<!--
--host string
: Host address of YugabyteDB Aeon (this should always be cloud.yugabyte.com). -->

-l, --logLevel string
: Specify the desired level of logging. `debug` or `info` (default).

--no-color
: Disable colors in output. `true` or `false` (default).

-o, --output string
: Specify the desired output format. `table` (default), `json`, or `pretty`.

--timeout duration
: Wait command timeout. For example, 5m, 1h. Default is 168h0m0s.

--wait
: For long-running commands such as creating or deleting a cluster, you can use the `--wait` flag to display progress in the shell. `true` or `false` (default). For example:

```sh
ybm cluster delete \
    --cluster-name=test-cluster \
    --wait
```

If you are using ybm with the `--wait` flag in your CI/CD system, you can set the environment variable `YBM_CI` to `true` to avoid generating unnecessary log lines.

## Autocompletion

You can configure command autocompletion for your shell using the `completion` command. For example:

```sh
ybm completion bash
```

This generates an autocompletion script for the specified shell. Available options are as follows:

- bash
- fish
- powershell
- zsh

### Bash

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

### fish

To load completions in your current shell session, use the following command:

```sh
ybm completion fish | source
```

To load completions for every new session, execute the following command:

```sh
ybm completion fish > ~/.config/fish/completions/ybm.fish
```

Start a new shell for the setup to take effect.

### PowerShell

To load completions in your current shell session, use the following command:

```sh
ybm completion powershell | Out-String | Invoke-Expression
```

To load completions for every new session, add the output of the preceding command to your PowerShell profile.

### Zsh

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
