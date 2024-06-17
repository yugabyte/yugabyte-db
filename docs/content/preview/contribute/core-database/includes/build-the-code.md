<!--
+++
private = true
+++
-->

Assuming [this repository][repo] is checked out in `~/code/yugabyte-db`, do the following:

```sh
cd ~/code/yugabyte-db
./yb_build.sh release
```

The command above will build the release configuration, add the binaries into the `build/release-<compiler>-dynamic-ninja` directory, and create a `build/latest` symlink to that directory.

{{< note title="Note" >}}

If you see errors, such as `internal compiler error: Killed`, the system has probably run out of memory.
Try again by running the build script with less concurrency, for example, `-j1`.

{{< /note >}}

{{< note title="Note" >}}

If you get an error message such as

> ValueError: Found no third-party release archives to download for OS type ubuntu20.04, compiler type matching gcc11, architecture x86_64, is_linuxbrew=False. See more details above.

it means that there is no [third-party download](#opt-yb-build) available for that build configuration.
Check the output that precedes the message for supported configurations.

{{< /note >}}

For more details about building and testing, refer to [Build and test][build-and-test].

[repo]: https://github.com/yugabyte/yugabyte-db
[build-and-test]: ../build-and-test
