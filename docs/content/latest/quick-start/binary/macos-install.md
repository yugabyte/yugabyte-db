## Prerequisites

<i class="fa fa-apple" aria-hidden="true"></i> macOS 10.12 (Sierra) or higher

## Download

Download the YugaByte CE package as shown below.

```{.sh .copy .separator-dollar}
$ wget https://downloads.yugabyte.com/yugabyte-ce-0.9.10.0-darwin.tar.gz
```
```{.sh .copy .separator-dollar}
$ tar xvfz yugabyte-ce-0.9.10.0-darwin.tar.gz && cd yugabyte-0.9.10.0/
```

## Configure

Setup loopback IP addresses on your localhost so that every node in the 3 node local cluster gets a unique IP address of its own.

```{.sh .copy}
sudo ifconfig lo0 alias 127.0.0.2
sudo ifconfig lo0 alias 127.0.0.3
```

Add some more loopback IP addresses to cover the add node scenarios of the [Explore Core Features](../../explore/) section.

```{.sh .copy}
sudo ifconfig lo0 alias 127.0.0.4
sudo ifconfig lo0 alias 127.0.0.5
sudo ifconfig lo0 alias 127.0.0.6
sudo ifconfig lo0 alias 127.0.0.7
```
