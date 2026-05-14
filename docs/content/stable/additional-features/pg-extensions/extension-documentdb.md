---
title: DocumentDB extension
headerTitle: DocumentDB extension
linkTitle: DocumentDB
description: Using the DocumentDB extension in YugabyteDB
tags:
  feature: tech-preview
menu:
  stable:
    identifier: extension-documentdb
    parent: pg-extensions
    weight: 20
type: docs
---

{{< warning title="Extension support" >}}
Support for the DocumentDB extension is in {{<tags/feature/tp>}}. It is not recommended for use in production.
{{< /warning >}}

The [DocumentDB](https://documentdb.io) extension adds a BSON data type and document-store APIs to YugabyteDB, enabling CRUD, aggregation, and indexing operations on JSON-style documents. A built-in gateway worker listens on a separate port and speaks the wire protocol used by MongoDB drivers and tools (for example, PyMongo or mongosh).

The extension ships as three components, all bundled with YugabyteDB:

- `documentdb_core` — adds the BSON type and core operators.
- `documentdb` — provides the CRUD, indexing, and aggregation API surface.
- `pg_documentdb_gw_host` — a background worker that translates wire-protocol commands into SQL calls into the extension.

## Set up DocumentDB

Before you can use the extension, set the following flags on every YB-Master and YB-TServer:

- `allowed_preview_flags_csv=ysql_enable_documentdb`
- `ysql_enable_documentdb=true`
- `enable_pg_cron=true` (the extension uses `pg_cron` for background maintenance)

By default the gateway listens on port `27017`. You can change this with the `documentdb_port` flag on YB-TServers.

For example, to start a single-node [yugabyted](../../../reference/configuration/yugabyted/) cluster with DocumentDB enabled:

```sh
./bin/yugabyted start \
    --master_flags "allowed_preview_flags_csv=ysql_enable_documentdb,ysql_enable_documentdb=true,enable_pg_cron=true" \
    --tserver_flags "allowed_preview_flags_csv=ysql_enable_documentdb,ysql_enable_documentdb=true,enable_pg_cron=true"
```

## Enable DocumentDB

Connect to the `yugabyte` database with [ysqlsh](../../../api/ysqlsh/) and create the extension:

```sql
CREATE EXTENSION documentdb CASCADE;
```

`CASCADE` automatically creates the `documentdb_core` dependency.

The gateway authenticates clients using SCRAM-SHA-256, so set a password on the YSQL user the application will use to connect:

```sql
ALTER USER yugabyte PASSWORD 'yugabyte';
```

After creating the extension, restart the cluster so the gateway background worker picks up the newly created schemas:

```sh
./bin/yugabyted stop
./bin/yugabyted start \
    --master_flags "allowed_preview_flags_csv=ysql_enable_documentdb,ysql_enable_documentdb=true,enable_pg_cron=true" \
    --tserver_flags "allowed_preview_flags_csv=ysql_enable_documentdb,ysql_enable_documentdb=true,enable_pg_cron=true"
```

The restart is a current limitation; see [Limitations](#limitations).

## Use DocumentDB

The gateway authenticates using SCRAM-SHA-256 over TLS with an auto-generated certificate. Connect with any MongoDB driver using the YSQL `yugabyte` user.

The following example uses the [PyMongo](https://pymongo.readthedocs.io/) Python driver. Install it with pip:

```sh
pip install pymongo
```

Then connect and run document operations:

```python
from pymongo import MongoClient

client = MongoClient(
    "mongodb://yugabyte:yugabyte@localhost:27017/"
    "?tls=true&tlsAllowInvalidCertificates=true&authMechanism=SCRAM-SHA-256"
)

db = client["quickstart"]
people = db["people"]

# Insert a single document.
people.insert_one({"name": "Alice", "age": 30, "city": "Anytown"})

# Insert multiple documents.
people.insert_many([
    {"name": "Bob", "age": 45, "city": "Othertown"},
    {"name": "Carol", "age": 29, "city": "Sometown"},
])

# Read.
for doc in people.find({"age": {"$gt": 30}}):
    print(doc)

# Update.
people.update_one({"name": "Alice"}, {"$set": {"age": 31}})

# Aggregate.
pipeline = [
    {"$match": {"age": {"$gte": 30}}},
    {"$group": {"_id": "$city", "count": {"$sum": 1}}},
]
for row in people.aggregate(pipeline):
    print(row)

# Delete.
people.delete_one({"name": "Carol"})
```

The same connection string works with the `mongosh` shell:

```sh
mongosh "mongodb://yugabyte:yugabyte@localhost:27017/?tls=true&tlsAllowInvalidCertificates=true&authMechanism=SCRAM-SHA-256"
```

## Limitations

- A cluster restart is required after running `CREATE EXTENSION documentdb`. The DocumentDB gateway background worker starts before the extension schemas exist and caches that state, so it cannot serve requests until the cluster is restarted. {{<issue 31353>}}
- Secondary indexes on collections are not yet supported. The extension relies on the RUM index access method, which is not currently available in YugabyteDB. Queries fall back to sequential scans of the underlying collection table. {{<issue 31634>}}
