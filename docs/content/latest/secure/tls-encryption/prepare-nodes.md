---
title: 1. Prepare nodes
linkTitle: 1. Prepare nodes
description: 1. Prepare nodes
headcontent: Generate the per-node configuration and prepare the nodes with the config data.
image: /images/section_icons/secure/prepare-nodes.png
aliases:
  - /secure/tls-encryption/prepare-nodes
menu:
  latest:
    identifier: prepare-nodes
    parent: tls-encryption
    weight: 10
isTocNested: true
showAsideToc: true
---

Before you can enable TLS encryptions, follow these steps to prepare each node in a YugabyteDB cluster.

## Basic setup

### Create a secure data directory

To generate and store the secure information, such as the root certificate, create a directory, `tls-certs`, in your root directory. Afer completing the preparation, you will copy this data inti a secure location and then delete this directory.

```sh
$ mkdir tls-certs
```

### Create a directory for configuration data of each node

Now create one directory for each node and put all the required data in that directory. This directory will eventually be copied into the respective nodes.

```sh
$ mkdir 127.0.0.1/ 127.0.0.2/ 127.0.0.3/
```

You should now have three directories named `127.0.0.1`, `127.0.0.2`, and `127.0.0.3`, representing your three nodes.

### Create the OpenSSL CA configuration

Create the file `rootCA.conf` in the `tls-certs` directory with the OpenSSL CA configuration.

```sh
$ cat > tls-certs/rootCA.conf
```

Paste the following example configuration into the file.

```sh
################################
# Example CA configuration file
################################

[ ca ]
default_ca = my_ca

[ my_ca ]
# Validity of the signed certificate in days.
default_days = 3650


# Text file with next hex serial number to use.
serial = ./serial.txt

# Text database file to use, initially empty.
database = ./index.txt

# Message digest algorithm. Do not use MD5.
default_md = sha256

# a section with a set of variables corresponding to DN fields
policy = my_policy

[ my_policy ]

# Policy for nodes and users. If the value is "match" then 
# field value must match the same field in the CA certificate.
# If the value is "supplied" then it must be present. Optional
# means it may be present.
organizationName = supplied
commonName = supplied

[req]
prompt=no
distinguished_name = my_distinguished_name
x509_extensions = my_extensions

[ my_distinguished_name ]
organizationName = Yugabyte
commonName = CA for YugabyteDB

[ my_extensions ]
keyUsage = critical,digitalSignature,nonRepudiation,keyEncipherment,keyCertSign
basicConstraints = critical,CA:true,pathlen:1
```

To save and close the file, enter `Ctl+D`.

### Set up the necessary files

Create the index file (`index.txt`) and database file (`serial.txt`) by running the following command.

```sh
$ touch index.txt ; echo '01' > serial.txt
```

## Generate root configuration

Now you will generate the root key file `rootCA.key` and the root certificate `rootCA.crt`.

To generate the root private key file `rootCA.key` in the `tls-certs` directory, run the following `openssl genrsa` command.

```sh
$ openssl genrsa -out tls-certs/rootCA.key
```

Change the permissions of the generated private key to allow only read permission by running the `chmod` command.

```sh
$ chmod 400 tls-certs/rootCA.key
```

Now generate the root certificate by running the following `openssl req` command.

```sh
$ openssl req -new                         \
            -x509                        \
            -config tls-certs/rootCA.conf  \
            -key tls-certs/rootCA.key      \
            -out tls-certs/rootCA.crt

```

You can verify the root certificate by running the following `openssl x509` command.

```sh
$ openssl x509 -in tls-certs/rootCA.crt -text -noout
```

You should see output similar to this:

```
Certificate:
    Data:
        Version: 3 (0x2)
        Serial Number: 9342236890667368184 (0x81a64af46bc73ef8)
    Signature Algorithm: sha256WithRSAEncryption
        Issuer: O=Yugabyte, CN=CA for YugabyteDB
        Validity
            Not Before: Dec 20 05:16:11 2018 GMT
            Not After : Jan 19 05:16:11 2019 GMT
        Subject: O=Yugabyte, CN=CA for YugabyteDB
        Subject Public Key Info:
            Public Key Algorithm: rsaEncryption
                Public-Key: (2048 bit)
                Modulus:
                    00:9e:c2:99:c8:10:38:12:a3:24:1b:2e:d5:de:30:
                    ...
                Exponent: 65537 (0x10001)
        X509v3 extensions:
            X509v3 Key Usage: critical
                Digital Signature, Non Repudiation, Key Encipherment, Certificate Sign
            X509v3 Basic Constraints: critical
                CA:TRUE, pathlen:1
    Signature Algorithm: sha256WithRSAEncryption
         51:b2:9c:4f:3d:7c:42:fc:93:e6:7b:f0:16:46:d2:21:4f:33:
         ...
```

Copy the generated root certificate file `rootCA.crt` to all three node directories.

```sh
$ cp rootCA.crt 127.0.0.1/; cp rootCA.crt 127.0.0.2/; cp rootCA.crt 127.0.0.3/
```

## Generate per-node configuration

Now you can generate the node key `node.key` and node certificate `node.crt` for each node.

### Generate configuration for each node

Repeat the steps in this section once for each node.
The IP address of each node is  `<node-ip-address>`

1. Generate a configuration file (`node.conf`) for a node, using the node's IP address (`<node-ip-address>`) as the directory name.

    ```sh
    $ cat > <node-ip-address>/node.conf
    ```

2. Add the following sample configuration content (use as-is, or customize as needed).

    {{< note title="Note" >}}
    
    Remember to replace the `<node-ip-address>` entry in the example configuration file below with the node name or IP address of each node.
    
    {{< /note >}}
    
    ```sh
    #################################
    # Example node configuration file
    #################################
    
    [ req ]
    prompt=no
    distinguished_name = my_distinguished_name
    
    [ my_distinguished_name ]
    organizationName = Yugabyte
    # Required value for commonName, do not change.
    commonName = <node-ip-address>
    ```

3. After pasting the content in step 2 and replacing `<node-ip-address>` with the node IP address, save and close the file by entering `Ctl+D`.

Repeat these steps for each of the three nodes. You should then have a copy of `node.conf` in the `127.0.0.1`, `127.0.0.2`, and `127.0.0.3` directories.

### Generate private key for each node

For each of the three nodes, generate the private key by running the following command, replacing `<node-ip-address>` with the node IP address.

{{< note title="Note" >}}

For YugabyteDB to recognize the file, it must be of the format `node.<commonName>.key`. In this example,
you are using the `<node-ip-address>` for the `<commonName>`, so the file names should be `node.127.0.0.1.key`,
`node.127.0.0.2.key`, and `node.127.0.0.3.key`.

{{< /note >}}

```sh
$ openssl genrsa -out <node-ip-address>/node.<node-ip-address>.key
$ chmod 400 <node-ip-address>/node.<node-ip-address>.key
```

### Generate the node certificates

Next, you need to generate the node certificate. This has two steps. First, create the certificate signing request (CSR) for each node, changing `<node-ip-address>`to the node IP address.

```sh
$ openssl req -new \
              -config <node-ip-address>/node.conf \
              -key <node-ip-address>/node.<node-ip-address>.key \
              -out <node-ip-address>/node.csr
```

Sign the node CSR with `ca.key` and `ca.crt`. Run the following command, changing `<node-ip-address>`to the node IP address.

```sh
$ openssl ca -config tls-certs/rootCA.conf \
             -keyfile tls-certs/rootCA.key \
             -cert tls-certs/rootCA.crt \
             -policy my_policy \
             -out <node-ip-address>/node.<node-ip-address>.crt \
             -outdir <node-ip-address>/ \
             -in <node-ip-address>/node.csr \
             -days 3650 \
             -batch
```

{{< note title="Note" >}}

The node key and crt should have `node.<name>.[crt | key]` naming format.

{{< /note >}}

You can verify the signed certificate for each of the nodes by doing the following:

```sh
$ openssl verify -CAfile tls-certs/ca.crt <node-ip-address>/node.<node-ip-address>.crt
```

You should see the following output, displaying the node IP address:

```
X.X.X.X/node.X.X.X.X.crt: OK
```

## Copy configuration files to the nodes

The files needed for each node are:

* `ca.crt`
* `node.<name>.crt`
* `node.<name>.key`

You can remove all other files in the node directories as they are unnecessary.

Upload the necessary information to each target node.

Create the directory that will contain the configuration files.

```sh
$ ssh <username>@<node-ip-address> mkdir ~/yugabyte-tls-config
```

Copy all the configuration files into the above directory by running the following commands, changing `<node-ip-address>`to the node IP address.

```sh
$ scp <node-ip-address>/ca.crt <user>@<node-ip-address>:~/yugabyte-tls-config/<node-ip-address>
```

```sh
$ scp <node-ip-address>/node.<node-ip-address>.crt <user>@<node-ip-address>:~/yugabyte-tls-config/<node-ip-address>
```

```sh
$ scp <node-ip-address>/node.<node-ip-address>.key <user>@<node-ip-address>:~/yugabyte-tls-config/<node-ip-address>
```

You can now delete, or appropriately secure, the directories you created for the nodes on the local machine.
