<!--
+++
private = true
+++
-->

By default, when running build, third-party libraries are not built, and pre-built libraries are downloaded.
We also use [Linuxbrew][linuxbrew] to provide some of the third-party dependencies.
The build scripts automatically install these in directories under `/opt/yb-build`.
In order for the build script to write under those directories, it needs proper permissions.
One way to do that is as follows:

```sh
sudo mkdir /opt/yb-build
sudo chown "$USER" /opt/yb-build
```

Alternatively, specify the build options `--no-download-thirdparty` and/or `--no-linuxbrew`.
Note that those options may require additional, undocumented steps.

[linuxbrew]: https://github.com/linuxbrew/brew
