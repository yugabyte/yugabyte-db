---
title: Create server certificates
linkTitle: Create server certificates
description: Create server certificates
headcontent: Generate server certificates and prepare the nodes for server-server encryption.
image: /images/section_icons/secure/prepare-nodes.png
aliases:
  - /secure/tls-encryption/prepare-nodes
  - /latest/secure/tls-encryption/prepare-nodes
menu:
  latest:
    identifier: prepare-nodes
    parent: tls-encryption
    weight: 10
isTocNested: true
showAsideToc: true
---

Before you can enable server-server and client-server encryptions using Transport Security Layer (TLS), you need to prepare each node in a YugabyteDB cluster.

## Create the server certificates

### Create a secure data directory

To generate and store the secure information, such as the root certificate, create a directory, `secure-data`, in your root directory. After completing the preparation, you will copy this data inti a secure location and then delete this directory.

```sh
$ mkdir secure-data
```

### Create temporary node directories

Now create one directory for each node and put all the required data in that directory. This files added to each directory will be copied into the `tls-cert` directory on each node.

```sh
$ mkdir 127.0.0.1/ 127.0.0.2/ 127.0.0.3/
```

You should now have three directories, named `127.0.0.1`, `127.0.0.2`, and `127.0.0.3`, representing the three nodes of your YugabyteDB cluster.

### Create the root configuration file

Create the file `root.conf` in the `yugabyte-tls-config` directory with the OpenSSL CA configuration.

```sh
$ cat > secure-data/ca.conf
```

Paste the following example root configuration into the file.

```sh
####################################
# Example CA root configuration file
####################################

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

### Add required files

Create the index file (`index.txt`) and database file (`serial.txt`) by running the following commands.

```sh
$ touch index.txt
$ echo '01' > serial.txt
```

### Generate the root key (ca.key)

To generate the root private key file (`ca.key`) in the `secure-data` directory, run the following `openssl genrsa` command.

```sh
$ openssl genrsa -out secure-data/ca.key
```

You should see output like this:

```
Generating RSA private key, 2048 bit long modulus
................+++
............................................+++
e is 65537 (0x10001)
```

Change the access permissions of the generated private key to allow read-only privileges by running the `chmod` command.

```sh
$ chmod 400 secure-data/ca.key
```

## Generate the root certificate file

Next, generate the root certificate file (`ca.crt`) by running the following `openssl req` command.

```sh
$ openssl req -new                      \
            -x509                       \
            -config secure-data/ca.conf \
            -key secure-data/ca.key     \
            -out secure-data/ca.crt
```

In the `secure-data` directory, you should now have the following three files:

- `ca.conf` - root configuration file
- `ca.key` - root key file
- `ca.crt` - root certificate file

You can verify the root certificate by running the following `openssl x509` command.

```sh
$ openssl x509 -in secure-data/ca.crt -text -noout
```

You should see output similar to this:

```
Certificate:
    Data:
        Version: 3 (0x2)
        Serial Number: 11693053643450365969 (0xa246125615723811)
    Signature Algorithm: sha256WithRSAEncryption
        Issuer: O=Yugabyte, CN=CA for YugabyteDB
        Validity
            Not Before: Feb 12 22:27:42 2020 GMT
            Not After : Mar 13 22:27:42 2020 GMT
        Subject: O=Yugabyte, CN=CA for YugabyteDB
        Subject Public Key Info:
            Public Key Algorithm: rsaEncryption
                Public-Key: (2048 bit)
                Modulus:
                    00:ba:8c:6e:d3:27:e9:03:9a:99:23:0b:e9:ef:9b:
                    2e:cb:d0:6d:89:ef:15:a2:77:0f:0c:d8:e9:cb:a2:
                    e4:33:cc:9a:3c:09:34:ef:1d:f4:7a:62:74:96:ef:
                    5b:2b:63:1d:6d:7d:c9:f7:e9:16:06:f7:76:55:52:
                    e0:4d:ba:5f:3e:af:46:c1:53:56:7a:6f:ee:33:ab:
                    a5:46:31:13:c8:b3:28:0a:ef:bc:89:6e:10:12:37:
                    dc:71:dd:b4:a3:25:47:38:7f:75:61:c3:7c:99:7d:
                    21:e7:00:ae:5e:18:0e:39:76:60:9d:f7:1e:1b:3b:
                    03:2b:56:b6:f9:30:7b:ba:8a:4b:d0:c4:33:6b:03:
                    c4:58:79:21:19:ce:1b:d5:f0:11:6e:a7:2e:1c:2b:
                    cd:5b:bd:a4:ce:33:69:d7:9a:4e:32:98:db:9d:35:
                    4c:82:e1:2f:36:a9:e7:f0:ba:d4:e8:a3:0d:bb:08:
                    7b:14:67:59:4b:7c:d2:4c:ad:6c:27:ac:aa:cd:67:
                    66:1c:df:c7:ef:bd:9f:43:71:d0:4f:e0:11:69:5a:
                    b3:2e:db:a1:d0:7c:b3:80:19:b2:f6:31:9d:bd:2a:
                    39:cb:f7:65:8e:74:3b:29:e7:7f:c7:6b:e8:1c:25:
                    56:e0:2d:2b:f2:9d:09:4a:5c:8a:86:7f:80:2a:e8:
                    f7:cd
                Exponent: 65537 (0x10001)
        X509v3 extensions:
            X509v3 Key Usage: critical
                Digital Signature, Non Repudiation, Key Encipherment, Certificate Sign
            X509v3 Basic Constraints: critical
                CA:TRUE, pathlen:1
    Signature Algorithm: sha256WithRSAEncryption
         a1:68:82:78:3d:72:68:ca:ad:e2:f6:8d:60:0d:fe:08:c0:5c:
         a0:73:2f:1c:1e:34:87:6f:31:2c:54:6a:2f:dd:1a:87:0e:01:
         74:d3:95:49:e0:bf:ab:b8:47:54:72:4e:8e:77:40:e9:06:ce:
         c3:95:f9:8d:e7:3a:82:73:63:b6:7f:05:36:31:63:66:18:8c:
         49:0b:ae:e5:ca:8b:62:cb:62:ac:4e:d3:be:b5:f6:ee:7e:44:
         5e:27:d2:c9:b5:10:cf:9e:09:ae:90:6d:1c:26:42:61:7c:f7:
         ec:95:b6:df:a6:ee:3c:cc:49:d6:29:bd:28:85:02:4d:57:84:
         e0:60:85:16:9c:b4:4f:94:8a:b0:76:96:d5:0a:91:a3:26:df:
         5b:4b:99:f2:32:0c:f9:2c:9a:e6:7a:bb:c4:a1:92:58:93:3e:
         b2:41:e8:dd:f8:68:04:a3:44:b7:02:68:4d:70:ee:c9:fb:e2:
         0b:a9:be:3b:4a:22:0a:ca:57:37:42:bb:e8:94:7e:53:43:19:
         15:65:db:84:65:3d:49:b3:04:aa:fe:f0:e9:3a:e5:9d:f6:07:
         ee:03:7b:fc:03:44:b8:f3:97:cc:ad:c0:39:58:66:10:76:e0:
         c4:0d:ef:e7:65:ab:bb:42:98:a0:b2:f5:a3:fe:d0:63:7c:46:
         2a:e7:7f:97
```

## Copy the root certificate to the node directories

Copy the generated root certificate file (`root.crt`) to all three node directories.

```sh
$ cp secure-data/ca.crt 127.0.0.1
$ cp secure-data/ca.crt 127.0.0.2
$ cp secure-data/ca.crt 127.0.0.3
```

## Generate key and certificate files for each node

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

Remember to replace the `<node-ip-address>` entry in the example configuration file below with the IP address for each node.

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

Repeat these three steps for each of the three nodes. You should then have a copy of `node.conf` in the `127.0.0.1`, `127.0.0.2`, and `127.0.0.3` directories.

### Generate private key files for each node

For each of the three nodes, generate the node private key by running the following command, replacing `<node-ip-address>` with the node IP address.

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

Sign each node CSR using the root key (`ca.key`) and root certificate (`ca.crt`). Run the following `openssl ca` command, changing `<node-ip-address>`to the node IP address.

```sh
$ openssl ca -config secure-data/ca.conf \
             -keyfile secure-data/ca.key \
             -cert secure-data/ca.crt \
             -policy my_policy \
             -out <node-ip-address>/node.<node-ip-address>.crt \
             -outdir <node-ip-address>/ \
             -in <node-ip-address>/node.csr \
             -days 3650 \
             -batch
```

For the `127.0.0.1` node, you should see the following output.

```
Using configuration from secure-data/ca.conf
Check that the request matches the signature
Signature ok
The Subject's Distinguished Name is as follows
organizationName      :ASN.1 12:'Yugabyte'
commonName            :ASN.1 12:'127.0.0.1'
Certificate is to be certified until Feb  9 23:01:41 2030 GMT (3650 days)

Write out database with 1 new entries
Data Base Updated
```

{{< note title="Note" >}}

Each node key and certificate should use the `node.<commonName>.[crt | key]` naming format.

{{< /note >}}

You can verify the signed certificate for each of the nodes by running the following `openssl verify` command:

```sh
$ openssl verify -CAfile secure-data/ca.crt <node-ip-address>/node.<node-ip-address>.crt
```

You should see the following output, displaying the node IP address:

```
X.X.X.X/node.X.X.X.X.crt: OK
```

## Copy configuration files to the nodes

The files needed for each node are:

- `ca.crt`
- `node.<name>.crt`
- `node.<name>.key`

You can remove all other files in the node directories as they are unnecessary.

Upload the necessary information to each target node.

Create the directory that will contain the configuration files.

```sh
$ ssh <username>@<node-ip-address> mkdir ~/certs
```

Copy all the configuration files into the above directory by running the following commands, changing `<node-ip-address>`to match the node IP address.

```sh
$ scp <node-ip-address>/ca.crt <user>@<node-ip-address>:~/certs/<node-ip-address>
```

```sh
$ scp <node-ip-address>/node.<node-ip-address>.crt <user>@<node-ip-address>:~/certs/<node-ip-address>
```

```sh
$ scp <node-ip-address>/node.<node-ip-address>.key <user>@<node-ip-address>:~/certs/<node-ip-address>
```

You can now delete, or appropriately secure, the directories you created for the nodes on the local machine.
