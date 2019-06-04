
## Install necessary packages

First install [Homebrew](https://brew`bash/) in case you do not already have it. We will use this to install the other required packages.

```bash
/usr/bin/ruby -e "$(
  curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

Install the following packages using Homebrew:

```bash
brew install autoconf automake bash bison ccache cmake  \
             coreutils flex gnu-tar icu4c libtool maven \
             ninja pkg-config pstree wget zlib python@2
```

{{< note title="Note on bash" >}}
YugaByte DB build scripts rely on Bash 4. Make sure that which bash outputs `/usr/local/bin/bash` before proceeding. You may need to put `/usr/local/bin` as the first directory on PATH in your `~/.bashrc` to achieve that.
{{< /note >}}

