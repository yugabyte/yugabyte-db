---
title: yugabyted
linkTitle: yugabyted
description: yugabyted
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    identifier: yugabyted
    parent: admin
    weight: 2459
isTocNested: true
showAsideToc: true
---

{{< note title="Note" >}}

`yugabyted` is under active development as we incrementally add new features and functionality and strive to continually simplify your experience. To learn and develop YugabyteDB applications, use `yugabyted` and `yb-ctl` to simplify basic operations and tasks. For production deployments, use `yb-tserver` and `yb-master`.

{{< /note >}}

Use `yugabyted` to 

## Syntax

```sh
yugabyted [-h] [ <command> ] [ <options> ]
```

- *command*: command to run
- *options*: one or more options separated by spaces.

## Commands

- [start]("#start") —  Start YugabyteDB.
- [stop](#stop) — Stop running YugabyteDB.
- [status](#status) — Print status of YugabyteDB.
- [version](#version) — Print the version of YugabyteDB and exit.
- [demo](#demo) — Load and interact with preset demo data.

### Command-line help

-h, --help

Print the help message and exit.

For help with specific commands, run 'yugabyted [command] -h'.

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
   [ --daemon t | f ]
   [ --callhome t | f ] 
   [ --ui t | f ]
```

### Options

These are the options.

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

Use the `yugabted stop` command to stop a YugabyteDB.

### Syntax

```sh
yugabyted stop [-h] [--config CONFIG] [--data_dir DATA_DIR]
```

## Options

[-h, --help]({{< relref "#h-help" >}})
: Print the command line help and exit.
  
[--config *config-file*]({{< relref "#config-config-file" >}})
: The path to the YugabyteDB configuration file.
  
[--data_dir *data-directory*]({{< relref "#data-dir-data-directory" >}})
: The directory where YugabyteDB will store data.

-----

## status

Use the `yugabyted status` command to check the status.

## Syntax

```
yugabyted status [ -h ] [ --config <config-file> ] [ --data_dir <data-directory> ]
```

## Options

[-h, --help]({{< relref "#h-help" >}})
: Print the command line help and exit.

[--config *config-file*]({{< relref "#config-config-file" >}})
: The path to the YugabyteDB configuration file.

[--data_dir *data-directory*]({{< relref "#data-dir-data-directory" >}})  
: The directory where YugabyteDB stores data.

-----

## version

Use the `yugabyted version` command to check the version number.

### Syntax

```
yugabyted version [ -h ] [ --config CONFIG ] [ --data_dir DATA_DIR ]
```

### Options

[-h | --help]({{< relref "#h-help" >}})
: Print the help message and exit.

[--config *config-file*]({{< relref "#config-config-file" >}})
: The path to the YugabyteDB configuration file.

[--data_dir *data-directory*]({{< relref "#data-dir-data-dictionary" >}})
: The directory where YugabyteDB stores data.

-----

## demo

Use the `yugabyted start` command to start a demo instance of YugabyteDB.

### Syntax

```
yugabyted demo [ -h ] [ --config CONFIG ] [ --data_dir DATA_DIR ]
```

### Options

[-h | --help]({{< relref "#h-help" >}})
: Print the help message and exit.

[--config *config-file*]({{< relref "#config-config-file" >}})
: The path to the YugabyteDB configuration file.

[--data_dir *data_directory*]({{< relref "#data-dir-data-directory" >}})
: The directory where YugabyteDB stores data.
