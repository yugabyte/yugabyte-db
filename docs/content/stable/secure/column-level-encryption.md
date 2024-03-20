---
title: Column-level encryption
headerTitle: Column-level encryption
linkTitle: Column-level encryption
description: Using column-level encryption in a YugabyteDB cluster.
headcontent: Enable encryption at rest with a user-generated key
menu:
  stable:
    identifier: column-level-encryption
    parent: secure
    weight: 745
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../column-level-encryption/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

</ul>

YugabyteDB provides column-level encryption to restrict access to sensitive data like addresses and credit card details. YugabyteDB uses the PostgreSQL `pgcrypto` extension to enable column-level encryption, `PGP_SYM_ENCRYPT` and `PGP_SYM_DECRYPT` functions of pgcrypto extension is used to encrypt and decrypt column data.

## Symmetric Encryption

Steps for enabling symmetric column encryption in YugabyteDB.

### Step 1. Enable the pgcrypto extension

Open the YSQL shell (ysqlsh), specifying the `yugabyte` user and prompting for the password.

```sh
$ ./ysqlsh -U yugabyte -W
```

When prompted for the password, enter the `yugabyte` user password. You should be able to log in and see a response similar to the following:

```output
ysqlsh (11.2-YB-2.5.0.0-b0)
Type "help" for help.

yugabyte=#
```

```sql
\c yugabyte yugabyte;
```

```output
You are now connected to database "yugabyte" as user "yugabyte".
```

Enable the `pgcrypto` extension on the YugabyteDB cluster:

```sql
create extension if not exists pgcrypto;
```

### Step 2. Insert data using PGP_SYM_ENCRYPT

Create the `employees` table, and insert data into the table using the `PGP_SYM_ENCRYPT` function for columns that need to be encrypted.

First, create the table:

```sql
create table employees ( empno int, ename text, address text, salary int, account_number text );
```

Next, encrypt account numbers in the `employees` table using the `PGP_SYM_ENCRYPT` function:

```sql
insert into employees values (1, 'joe', '56 grove st',  20000, PGP_SYM_ENCRYPT('AC-22001', 'AES_KEY'));

insert into employees values (2, 'mike', '129 81 st',  80000, PGP_SYM_ENCRYPT('AC-48901', 'AES_KEY'));

insert into employees values (3, 'julia', '1 finite loop',  40000, PGP_SYM_ENCRYPT('AC-77051', 'AES_KEY'));
```

### Step 3. Verify column encryption

Review the encrypted `account_number` data, as shown below:

```sql
select ename, account_number from employees limit 1;
```

```output
 ename |               account_number
-------+-------------------------------------------------
 joe   | \xc30d04070302ee4c6d5f6656ace96ed23901f56c717d4e
 162b6639429f516b5103acebc4bc91ec15df06c30e29e6841f4a5386
 e7698bfebb49a8660f9ae4b3f34fede3f28c9c7bb245bd
(1 rows)
```

### Step 4. Query using PGP_SYM_DECRYPT

Decrypt the account numbers using the `PGP_SYM_DECRYPT` function as shown here. In order to retrieve the encrypted column data, use `PGP_SYM_DECRYPT` function to decrypt the data. The Decryption function needs to be used in both SELECT and WHERE clause depending on the query.

To allow the decryption, the field name is also cast to the binary data type with the syntax: `account_number::bytea`.

```sql
select PGP_SYM_DECRYPT(account_number::bytea, 'AES_KEY') as AccountNumber
       from employees;
```

```output
 accountnumber
---------------
 AC-22001
 AC-48901
 AC-77051
(3 rows)
```

## Asymmetric Encryption

Asymmetric Encryption, also known as Public-Key cryptography can be used with YugabyteDB for enabling column-level encryption. YugabyteDB can be configured with generated public/private keys or corporate GPG keys for encrypting column data.

The example below walks through configuring YugabyteDB cluster with a new set of keys (public and private).

### Step 1. Generate an RSA key pair

* Start by generating a new public and private RSA key pair using the `gpg` key generator:

    ```sh
    $ gpg --gen-key
    ```

* After going through the configuration prompts, an RSA key gets generated:

    ```output
    public and secret key created and signed.

    pub   rsa2048 2020-11-09 [SC] [expires: 2022-11-09]
          043E14210E7628F93383D78EA2969FF91871CE06
    uid   ybadmin <ybadmin@yugabyte.com>
    sub   rsa2048 2020-11-09 [E] [expires: 2022-11-09]
    ```

* Next, export the public and private keys of the newly generated RSA key:

  * Private Key

    ```sh
    $ gpg --export-secret-keys \
          --armor 043E14210E7628F93383D78EA2969FF91871CE06 > ./private_key.txt
    ```

  * Public Key

    ```sh
    $ gpg --export --armor 043E14210E7628F93383D78EA2969FF91871CE06 > ./public_key.txt
    ```

### Step 2. Enable the pgcrypto extension

* Open the YSQL shell (ysqlsh), specifying the `yugabyte` user and prompting for the password.

    ```sh
    $ ./ysqlsh -U yugabyte -W
    ```

    When prompted for the password, enter the `yugabyte` user password.

* Enable the `pgcrypto` extension on the YugabyteDB cluster:

    ```sql
    create extension if not exists pgcrypto;
    ```

### Step 3. Insert data using pgp_pub_encrypt

Create the `employees` table and insert data into the table using the generated public key for encrypting column data

```sql
create table employees ( empno int, ename text, address text, salary int, account_number text );
```

In this example, account numbers of `employees` table are encrypted using the `pgp_pub_encrypt` function and the generated Public key.

```sql
insert into employees values (1, 'joe', '56 grove st',  20000, PGP_PUB_ENCRYPT('AC-22001', dearmor('-----BEGIN PGP PUBLIC KEY BLOCK----- XXXX  -----END PGP PUBLIC KEY BLOCK-----')));

insert into employees values (2, 'mike', '129 81 st',  80000, PGP_PUB_ENCRYPT('AC-48901', dearmor('-----BEGIN PGP PUBLIC KEY BLOCK----- XXXX  -----END PGP PUBLIC KEY BLOCK-----')));

insert into employees values (3, 'julia', '1 finite loop',  40000, PGP_PUB_ENCRYPT('AC-77051', dearmor('-----BEGIN PGP PUBLIC KEY BLOCK----- XXXX  -----END PGP PUBLIC KEY BLOCK-----')));
```

### Step 4. Verify column encryption

Verify that the data for the column `account_number` is encrypted as shown below.

```sql
select ename, account_number from employees limit 1;
```

```output
 ename |                   account_number
-------+------------------------------------------------------------
 julia | \xc1c04c039bd2f02876cc14ae0107ff44e68e5a4bb35784b426f4aeb46
 70976127d64e731cf8f70343b100ea0ed60b3de191fa19e245c4ce9b0289e44b53b
 7d3c42b8187487b3b0bb8ebed518a248ca3c1d663174d1c9d6769f7840ddbd8508d
 d4b91dcf77183779ff15b003431a7d05a1aef4b09313b602bcc2491cc2e417d5c39
 269230e032252547ce1fd51f27be0cc43c5fd75f35b21e0a72e8e
(1 row)
```

### Step 5. Query using pgp_pub_decrypt

Use `pgp_pub_decrypt` and the private key to decrypt column data. To retrieve the encrypted column data, use the `pgp_pub_decrypt` function to decrypt the data and wrap the PGP private key with the `dearmor` function to convert the private key into PGP ASCII-armor format. The Decryption function needs to be used in both SELECT and WHERE clauses, depending on the query.

To allow the decryption, the field name is also cast to the binary data type with the syntax: `account_number::bytea`.

```sql
select PGP_PUB_DECRYPT(account_number::bytea,dearmor('-----BEGIN PGP PRIVATE KEY BLOCK----- XXXX  -----END PGP PRIVATE KEY BLOCK-----'),'PRIVATE-KEY-PASSWORD') as AccountNumber from employees;
```

```output
 accountnumber
---------------
 AC-22001
 AC-48901
 AC-77051
(3 rows)
```
