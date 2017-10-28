## Prerequisites

a) Supported operating systems are

<i class="icon-centos"></i> Centos 7+

b) You must have `python` installed on your localhost.

## Download

Download the YugaByte CE package as shown below.


```sh
$ wget https://downloads.yugabyte.com/yugabyte-ce-0.9.0.0-centos.tar.gz
$ tar xvfz yugabyte-ce-0.9.0.0-centos.tar.gz
$ cd yugabyte-0.9.0.0/
```

## Configure

Run the **configure** script to ensure all dependencies get auto-installed. This script will also install a couple of libraries (`cyrus-sasl`, `cyrus-sasl-plain` and `file`) and will request for a sudo password in case you are not running the script as root.

```sh
$ ./bin/configure
```