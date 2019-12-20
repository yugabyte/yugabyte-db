---
title: yugabyted
linkTitle: yugabyted
description: yugabyted
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    identifier: yugabyted
    parent: configuration
    weight: 2451
isTocNested: true
showAsideToc: true
---

Use the `yugabyted` binary and its options to configure a one-node YugabyteDB cluster. The `yugabyted` executable file is located in the `bin` directory of YugabyteDB home.

{{< note title="Note" >}}

`yugabyted` is under active development with new features and functionality. You can use `yugabyted` along with the [`yb-ctl`](../../../admin/yb-ctl) utility to quickly develop and test YugabyteDB on your local environment. For production deployments, configure your YugabyteDB clusters using [`yb-tserver`](../yb-tserver) and [`yb-master`](../yb-master).

{{< /note >}}

## Syntax

```sh
yugabyted [-h] [ <command> ] [ <options> ]
```

- *command*: command to run
- *options*: one or more options separated by spaces.

### Command-line help

You can access the overview command line help for `yugabyted` by running one of the following examples from the YugabyteDB home:

```sh
$ ./bin/yugabyted -h
```

```sh
$ ./bin/yugabyted -help
```

For help with specific `yugabyted` commands, run 'yugabyted [ command ] -h'. For example, you can print the command line help for the `yugabyted start` command by running the following:

```sh
$ ./bin/yugabyted start -h
```

## Commands

The following commands are available:

- [start](#start)
- [stop](#stop)
- [status](#status)
- [version](#version)
- [demo](#demo)

-----

### start

Use the `yugabyted start` command to start YugabyteDB.

### Syntax

```sh
 yugabyted start
   [ -h | --help ] 
   [ --config <config-file> ]
   [ --data_dir <data-dir> ]
   [ --log_dir <log-dir> ]  
   [ --ycql_port <ycql-port> ]
   [ --ysql_port <ysql-port> ]
   [ --master_rpc_port <master-rpc-port> ]
   [ --tserver_rpc_port <tserver-rpc-port> ]
   [ --master_webserver_port <master-webserver-port> ]
   [ --tserver_webserver_port <tserver-webserver-port> ]
   [ --webserver_port <webserver-port> ]
   [ --bind_ip BIND_IP ] 
   [ --daemon <bool> ]
   [ --callhome <bool> ] 
   [ --ui <bool> ]
```

### Options


[-h | --help]({{< relref "#h-help" >}})
: Print the commmand-line help and exit.

[--config *config-file*]({{< relref "#config-config-file" >}})
: The path to the configuration file.

[--data_dir *data-directory*]({{< relref "#data-dir-data-dictionary" >}})
: The directory where YugabyteDB stores data.

[--log_dir *log-directory*]({{< relref "#log-dir-log-directory" >}})
: The directory to store YugabyteDB logs.

[--ycql_port *ycql-port*]({{< relref "#ycql-port-ycql-port" >}})
: The port on which YCQL will run.

[--ysql_port *ysql-port*]({{< relref "#ysql-port-ysql-port" >}})
: The port on which YSQL will run.

[--master_rpc_port *master-rpc-port*]({{< relref "#master-rpc-port-master-rpc-port" >}})
: The port on which yb-master will listen for RPC calls.

[--tserver_rpc_port *tserver-rpc-port*]({{< relref "#tserver-rpc-port-tserver-rpc-port" >}})
: The port on which YB-TServer will listen for RPC calls.

[--master_webserver_port *master-webserver-port*]({{< relref "#master-webserver-port-master-webserver-port" >}})
: The port on which YB-Master webserver will run.

[--tserver_webserver_port *tserver-webserver-port*]({{< relref "#tserver-webserver-port-tserver-webserver-port" >}})
: The port on which YB-TServer webserver will run.

[--webserver_port *webserver-port*]({{< relref "#webserver-port-webserver-port" >}})
: The port on which main webserver will run.

[--bind_ip *bind-ip*]({{< relref "#bind-ip-bind-ip" >}})
: The IP address to which `yugabyted` processes will bind.

[--daemon *bool* ]({{< relref "#daemon-bool" >}})
: Enable or disable running `yugabyted` in the background as a daemon. Does not persist on restart. Default is `true`.

[--callhome *bool*]({{< relref "#callhome-bool" >}})
: Enable or disable the "call home" feature that sends analytics data to Yugabyte. Default is `true`.

[--ui *bool*]({{< relref "#ui-bool" >}})
: Enable or disable the webserver UI. Default is `true`.

-----
## stop

Use the `yugabted stop` command to stop a YugabyteDB cluster.

### Syntax

```sh
yugabyted stop [ -h ] [ --config <config-file> ] [ --data_dir <data-directory> ]
```

## Options

[-h, --help]({{< relref "#h-help-1" >}})
: Print the command line help and exit.
  
[--config *config-file*]({{< relref "#config-config-file-1" >}})
: The path to the YugabyteDB configuration file.
  
[--data_dir *data-directory*]({{< relref "#data-dir-data-directory-1" >}})
: The directory where YugabyteDB will store data.

-----

## status

Use the `yugabyted status` command to check the status.

## Syntax

```
yugabyted status [ -h | --help ] [ --config <config-file> ] [ --data_dir <data-directory> ]
```

## Options

[-h, --help]({{< relref "#h-help-2" >}})
: Print the command line help and exit.

[--config *config-file*]({{< relref "#config-config-file-2" >}})
: The path to the YugabyteDB configuration file.

[--data_dir *data-directory*]({{< relref "#data-dir-data-directory-2" >}})  
: The directory where YugabyteDB stores data.

-----

## version

Use the `yugabyted version` command to check the version number.

### Syntax

```
yugabyted version [ -h | --help ] [ --config <config-file> ] [ --data_dir <data-directory> ]
```

### Options

[-h | --help]({{< relref "#h-help-3" >}})
: Print the help message and exit.

[--config *config-file*]({{< relref "#config-config-file-3" >}})
: The path to the YugabyteDB configuration file.

[--data_dir *data-directory*]({{< relref "#data-dir-data-dictionary-3" >}})
: The directory where YugabyteDB stores data.

-----

## demo

Use the `yugabyted start` command to start a demo instance of YugabyteDB.

### Syntax

```
yugabyted demo [ -h | -help ] [ --config <config-file> ] [ --data_dir <data-directory> ]
```

### Options

[-h | --help]({{< relref "#h-help-4" >}})
: Print the help message and exit.

[--config *config-file*]({{< relref "#config-config-file-4" >}})
: The path to the YugabyteDB configuration file.

[--data_dir *data_directory*]({{< relref "#data-dir-data-directory-4" >}})
: The directory where YugabyteDB stores data.
