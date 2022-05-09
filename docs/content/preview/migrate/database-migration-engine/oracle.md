
[TODO]

Create a database user and provide the user with READ access to all the resources which need to be migrated.

- You'll need to provide the user and the source database details in the subsequent invocations of yb_migrate. For convenience, you can populate the information in the following environment variables:

```sh
export SOURCE_DB_TYPE=oracle
export SOURCE_DB_HOST=localhost
export SOURCE_DB_PORT=1521
export SOURCE_DB_USER=ybmigrate
export SOURCE_DB_PASSWORD=password
export SOURCE_DB_NAME=pdb1
export SOURCE_DB_SCHEMA=sakila
```
