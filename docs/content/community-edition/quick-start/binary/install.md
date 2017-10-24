## Prerequisites

Supported operating systems are

- Linux: Centos 7 or higher
- Mac OS X

## Download

Download the YugaByte CE package [here](http://www.yugabyte.com#download). Thereafter, follow the instructions below.

## Install

On your localhost, execute the following commands.

```sh
$ mkdir ~/yugabyte
$ tar xvfz yugabyte.ce.<version>.tar.gz -C yugabyte
$ cd yugabyte
```

## Configure

- For Linux

Run the **configure** script to ensure all dependencies get auto-installed. This script will also install a couple of libraries (`cyrus-sasl`, `cyrus-sasl-plain` and `file`) and will request for a sudo password in case you are not running the script as root.

```sh
$ ./bin/configure
```

- For Mac OS

Setup loopback IP addresses on your localhost so that every node in the 3 node local cluster gets a unique IP address of its own.

```sh
$ sudo ifconfig lo0 alias 127.0.0.2
$ sudo ifconfig lo0 alias 127.0.0.3
```