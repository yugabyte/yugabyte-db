
## Install necessary packages

First install [Homebrew](https://brew.sh/) in case you do not already have it. We will use this to install the other required packages.

```sh
/usr/bin/ruby -e "$(
  curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

Install the following packages using Homebrew:

```sh
brew install autoconf automake bash ccache cmake  \
             coreutils flex gnu-tar icu4c libtool \
             maven ninja pkg-config pstree wget \
             zlib python@2
```

We need an older version of bison to correctly compile our code. The following command installs the 3.4.1 version of bison.

```sh
brew install \
https://raw.githubusercontent.com/Homebrew/homebrew-core/ee89a1d59df03f495a85c15e253b60299082ab9d/Formula/bison.rb
```

{{< note title="Note on bash" >}}
YugabyteDB build scripts rely on Bash 4. Make sure that `which bash` outputs `/usr/local/bin/bash` before proceeding. You may need to put `/usr/local/bin` as the first directory on PATH in your `~/.bashrc` to achieve that.
{{< /note >}}
