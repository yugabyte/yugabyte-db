## Prerequisites

a) <i class="fab fa-apple" aria-hidden="true"></i> macOS 10.12 (Sierra) or higher

b) Verify that you have python2 installed. Support for python3 is in the works.

```sh
$ python --version
```

```
Python 2.7.10
```

c) Make sure that your file limits for kern.maxfiles and kern.maxfilesperproc are 1048576. Edit `/etc/sysctl.conf` on High Sierra if necessary.

## Download

Download the YugaByte DB CE package as shown below.

```sh
$ wget https://downloads.yugabyte.com/yugabyte-ce-1.1.14.0-darwin.tar.gz
```

```sh
$ tar xvfz yugabyte-ce-1.1.14.0-darwin.tar.gz && cd yugabyte-1.1.14.0/
```

## Configure

Setup loopback IP addresses on your localhost so that every node in the 3 node local cluster gets a unique IP address of its own.

```sh
sudo ifconfig lo0 alias 127.0.0.2
sudo ifconfig lo0 alias 127.0.0.3
```


Add some more loopback IP addresses to cover the add node scenarios of the [Explore Core Features](../../explore/) section.

```sh
sudo ifconfig lo0 alias 127.0.0.4
sudo ifconfig lo0 alias 127.0.0.5
sudo ifconfig lo0 alias 127.0.0.6
sudo ifconfig lo0 alias 127.0.0.7
```
