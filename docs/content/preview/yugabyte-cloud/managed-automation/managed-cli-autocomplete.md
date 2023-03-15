


## ZSH

If shell completion is not already enabled in your environment, you can turn it on by running the following command once:

```sh
echo "autoload -U compinit; compinit" >> .zshrc
```

To load completions in your current shell session:

```sh
source <(ybm completion zsh); compdef _ybm ybm
```

To load completions for every new session, execute once:

```sh
#### for Linux:
ybm completion zsh > "${fpath[1]}/_ybm"

#### for macOS:
ybm completion zsh > $(brew --prefix)/share/zsh/site-functions/_ybm
```

You will need to start a new shell for this setup to take effect.


## BASH

YBM CLI autocompletion depends on the 'bash-completion' package.
If it is not installed already, you can install it via your OS's package manager.

To load completions in your current shell session:

```sh
source <(ybm completion bash)
```

To load completions for every new session, execute once:

```sh
#### for Linux:
ybm completion bash > /etc/bash_completion.d/ybm

#### for macOS:
ybm completion bash > $(brew --prefix)/etc/bash_completion.d/ybm
```

You will need to start a new shell for this setup to take effect.

## Powershell

To load completions in your current shell session:

```sh
ybm completion powershell | Out-String | Invoke-Expression
```

To load completions for every new session, add the output of the above command
to your powershell profile.

## FISH

To load completions in your current shell session:

```sh
ybm completion fish | source
```

To load completions for every new session, execute once:

```sh
ybm completion fish > ~/.config/fish/completions/ybm.fish
```

You will need to start a new shell for this setup to take effect.