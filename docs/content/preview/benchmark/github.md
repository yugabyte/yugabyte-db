<!--
+++
private=true
+++
-->

Install sysbench using the following steps:

```sh
$ cd $HOME
$ git clone https://github.com/yugabyte/sysbench.git
$ cd sysbench
$ ./autogen.sh && ./configure --with-pgsql && make -j && sudo make install
```

This installs the sysbench utility in `/usr/local/bin`.

Make sure you have the [YSQL shell](../../api/ysqlsh/) `ysqlsh` exported to the `PATH` variable.

```sh
$ export PATH=$PATH:/path/to/ysqlsh
```
