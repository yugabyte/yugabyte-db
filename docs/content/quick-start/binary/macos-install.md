## Prerequisites

a) <i class="fa fa-apple" aria-hidden="true"></i> macOS 10.12 (Sierra) or higher

b) You must have `python` installed on your localhost.

## Download

Download the YugaByte CE package as shown below.

```sh
$ wget https://downloads.yugabyte.com/yugabyte-ce-0.9.0.1-darwin.tar.gz
$ tar xvfz yugabyte-ce-0.9.0.1-darwin.tar.gz
$ cd yugabyte-0.9.0.1/
```

## Configure

Setup loopback IP addresses on your localhost so that every node in the 3 node local cluster gets a unique IP address of its own.

```sh
$ sudo ifconfig lo0 alias 127.0.0.2
$ sudo ifconfig lo0 alias 127.0.0.3
```

Add some more IP addresses to cover the add node scenarios of the [Explore Core Features](/explore/) section.

```sh
$ sudo ifconfig lo0 alias 127.0.0.4
$ sudo ifconfig lo0 alias 127.0.0.5
$ sudo ifconfig lo0 alias 127.0.0.6
$ sudo ifconfig lo0 alias 127.0.0.7
```
