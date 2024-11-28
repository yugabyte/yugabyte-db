<!--
+++
private=true
+++
-->

* **Command-line tools for Xcode** on macOS.

    ```sh
    $ xcode-select --install
    ```

    Xcode is many gigabytes. Install the command-line tools unless you actually need the full Xcode.

* [**Homebrew**](https://brew.sh) on macOS.

* **Python** (version 3.10 or earlier): You need `python` to be available somewhere in your shell path.

    Recent versions of macOS have only a `python3` executable, as does the Homebrew install. You can use [pyenv](https://github.com/pyenv/pyenv) to manage multiple versions of python on your system. Make sure to point to Python version 3.10 or earlier.

* **Hugo**: `brew install hugo` installs the latest version.

* **Go**: `brew install go` installs the latest version.

* **Git client**: The system Git binary is out of date, but works. If you like, you can use Homebrew to get a newer version (`brew install git`).
