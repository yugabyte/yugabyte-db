<!--
+++
private = true
block_indexing = true
+++
-->

[CMake][cmake] 3.31.0 or higher is required.

[cmake]: https://cmake.org

The CMake version in the package manager is too old, so manually download a release as follows:

```sh
mkdir ~/tools
curl -L "https://github.com/Kitware/CMake/releases/download/v3.31.0/cmake-3.31.0-linux-x86_64.tar.gz" | tar xzC ~/tools
# Also add the following line to your .bashrc or equivalent.
export PATH="$HOME/tools/cmake-3.31.0-linux-x86_64/bin:$PATH"
```
