---
title: 1. Prepare Nodes
linkTitle: 1. Prepare Nodes
description: 1. Prepare Nodes
headcontent: Generate the per-node config and prepare the nodes with the config data.
image: /images/section_icons/secure/prepare-nodes.png
aliases:
  - /secure/tls-encryption/prepare-nodes
menu:
  latest:
    identifier: secure-tls-encryption-prepare-nodes
    parent: secure-tls-encryption
    weight: 721
isTocNested: true
showAsideToc: true
---

This page describes how to prepare each node in a YugaByte DB cluster to enable TLS encryption.

## Basic setup


### Create a secure data directory

We will generate and store the secure info such as the root certificate in the `secure-data` directory. Once the setup is done, we will copy this data in a secure location and delete this directory.
<div class='copy separator-dollar'>
```sh
$ mkdir secure-data
```
</div>

### Prepare a IP_ADDRESSES env variable

In this example, we assume a 3 node cluster, with the variables `ip1`, `ip2` and `ip3` representing the ip addresses for the three nodes. Create a variable `IP_ADDRESSES` is a space-separated list of the IP addresses of the various nodes. We will use this variable to loop over all the nodes when needed.
<div class='copy separator-dollar'>
```sh
$ export IP_ADDRESSES="$ip1 $ip2 $ip3 ..."
```
</div>

{{< tip title="Tip" >}}
Add the desired set of IP addresses or node names into the `IP_ADDRESSES` variable as shown above. Remember to add exactly one entry for each node in the cluster.
{{< /tip >}}


### Create a directory for config data of each node

We will create one directory per node and put all the required data in that directory. This directory will eventually be copied into the respective nodes.
<div class='copy separator-dollar'>
```sh
$ for node in $IP_ADDRESSES;
do
  mkdir $node
done
```
</div>

### Create the OpenSSL CA configuration

Create the file ca.conf in the secure-data directory with the OpenSSL CA configuration.
<div class='copy separator-dollar'>
```sh
$ cat > secure-data/ca.conf
```
</div>

Paste the following example config into the file.

```{.sh .copy}
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
organizationName = YugaByte
commonName = CA for YugaByte DB

[ my_extensions ]
keyUsage = critical,digitalSignature,nonRepudiation,keyEncipherment,keyCertSign
basicConstraints = critical,CA:true,pathlen:1
```

### Setup the necessary files

Delete the existing index and database files.
<div class='copy separator-dollar'>
```sh
$ rm -f index.txt serial.txt
```
</div>

Create the index and database file.
<div class='copy separator-dollar'>
```sh
$ touch index.txt ; echo '01' > serial.txt
```
</div>


## Generate root config

In this section, we will generate the root key file `ca.key` and the root certificate `ca.crt`.

We will generate the root private key file `ca.key` in the `secure-data` directory using the `openssl genrsa` command as shown below.
<div class='copy separator-dollar'>
```sh
$ openssl genrsa -out secure-data/ca.key 2048
```
</div>

Change the permissions of the generated private key as follows.
<div class='copy separator-dollar'>
```sh
chmod 400 secure-data/ca.key
```
</div>

Now generate the root certificate.
<div class='copy separator-dollar'>
```sh
openssl req -new                         \
            -x509                        \
            -config secure-data/ca.conf  \
            -key secure-data/ca.key      \
            -out secure-data/ca.crt

```
</div>

You can verify the root certificate by doing the following:
<div class='copy separator-dollar'>
```sh
$ openssl x509 -in secure-data/ca.crt -text -noout
```
</div>

You should see output that looks as follows:

```
Certificate:
    Data:
        Version: 3 (0x2)
        Serial Number: 9342236890667368184 (0x81a64af46bc73ef8)
    Signature Algorithm: sha256WithRSAEncryption
        Issuer: O=YugaByte, CN=CA for YugaByte DB
        Validity
            Not Before: Dec 20 05:16:11 2018 GMT
            Not After : Jan 19 05:16:11 2019 GMT
        Subject: O=YugaByte, CN=CA for YugaByte DB
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

Copy the generated root certificate file `ca.crt` to all the node directories.
<div class='copy separator-dollar'>
```sh
$ for node in $IP_ADDRESSES;
do
  cp secure-data/ca.crt $node/;
done
```
</div>

## Generate per-node config

In this section, we will generate the node key `node.key` and node certificate `node.crt` for each node. 

### Generate config for each node

Repeat the steps in this section once for each node.

{{< tip title="tip" >}}
The IP address of each node is denoted by the variable `$NODE_IP_ADDRESS` below.
{{< /tip >}}

Generate a config file named `node.conf` for each node in the appropriate node directory as shown below. 
<div class='copy separator-dollar'>
```sh
$ cat > $NODE_IP_ADDRESS/node.conf
```
</div>

There is a sample config below that you can use. You can customize this file as needed. Move the resulting `node.conf` file into the appropriate node directory.

{{< note title="Note" >}}
Remember to replace the `<NODE_IP_ADDRESS>` entry in the example config file below with the node name or IP address of each node.
{{< /note >}}

```{.sh .copy}
################################
# Example node configuration file
################################

[ req ]
prompt=no
distinguished_name = my_distinguished_name

[ my_distinguished_name ]
organizationName = YugaByte
# Required value for commonName, do not change.
commonName = <NODE_IP_ADDRESS>
```

### Generate private key for each node

You can generate the private key for each of the nodes as follows.

{{< note title="Note" >}}
The file names must be of the format `node.<commonName>.key` for YugaByte DB to recognize the file.
{{< /note >}}
<div class='copy separator-dollar'>
```sh
$ for node in $IP_ADDRESSES;
do
  openssl genrsa -out $node/node.$node.key 2048
  chmod 400 $node/node.$node.key
done
```
</div>

### Generate the node certificates

Next, we need to generate the node certificate. This has two steps. First, create the certificate signing request (CSR) for each node.
<div class='copy separator-dollar'>
```sh
$ for node in $IP_ADDRESSES;
do
  openssl req -new                       \
              -config $node/node.conf    \
              -key $node/node.$node.key  \
              -out $node/node.csr
done
```
</div>

Sign the node CSR with ca.key and ca.crt
<div class='copy separator-dollar'>
```sh
$ for node in $IP_ADDRESSES;
do
  openssl ca -config secure-data/ca.conf   \
             -keyfile secure-data/ca.key   \
             -cert secure-data/ca.crt      \
             -policy my_policy             \
             -out $node/node.$node.crt     \
             -outdir $node/                \
             -in $node/node.csr            \
             -days 3650                    \
             -batch
done
```
</div>

{{< note title="Note" >}}
The node key and crt should have `node.<name>.[crt | key]` naming format.
{{< /note >}}

You can verify the signed certificate for each of the nodes by doing the following:
<div class='copy separator-dollar'>
```sh
$ openssl verify -CAfile secure-data/ca.crt $node/node.$node.crt
```
</div>

You should see the following output:
```
X.X.X.X/node.X.X.X.X.crt: OK
```

## Copy config to nodes

The files needed for each node are:

* `ca.crt`
* `node.<name>.crt`
* `node.<name>.key`

You can remove all other files in the node directories as they are unnecessary.

Upload the necessary information to the target node.
<div class='copy separator-dollar'>
```sh
$ for node in $IP_ADDRESSES;
do
  # Create the directory that will contain the config files.
  ssh <username>@$node mkdir ~/yugabyte-tls-config

  # Copy all the config files into the above directory.
  scp $node/ca.crt <user>@$node:~/yugabyte-tls-config/$NODE_IP
  scp $node/node.$node.crt <user>@$node:~/yugabyte-tls-config/$NODE_IP
  scp $node/node.$node.key <user>@$node:~/yugabyte-tls-config/$NODE_IP
done
```
</div>

You can now delete or appropriately secure the directories we created for the various nodes on the local machine.
