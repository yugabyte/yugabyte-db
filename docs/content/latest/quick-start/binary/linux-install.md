## Prerequisites

a) One of the following operating systems

<i class="icon-centos"></i> CentOS 7

<i class="icon-ubuntu"></i> Ubuntu 16.04+

b) Verify that you have python2 installed. Support for python3 is in the works.

```sh
$ python --version
```

```
Python 2.7.10
```

## Download

Download the YugabyteDB CE package as shown below.

```sh
$ wget https://downloads.yugabyte.com/yugabyte-2.0.0.0-linux.tar.gz
```

```sh
$ tar xvfz yugabyte-2.0.0.0-linux.tar.gz && cd yugabyte-2.0.0.0/
```

## Configure

You can do this as shown below.

```sh
$ ./bin/post_install.sh
```
