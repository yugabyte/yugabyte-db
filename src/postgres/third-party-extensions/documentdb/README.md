# Introduction

`DocumentDB` is a MongoDB compatible open source document database built on PostgreSQL. It offers a native implementation of a document-oriented NoSQL database, enabling seamless CRUD (Create, Read, Update, Delete) operations on BSON(Binary JSON) data types within a PostgreSQL framework. Beyond basic operations, DocumentDB empowers users to execute complex workloads, including full-text searches, geospatial queries, and vector search, delivering robust functionality and flexibility for diverse data management needs.

[PostgreSQL](https://www.postgresql.org/about/) is a powerful, open source object-relational database system that uses and extends the SQL language combined with many features that safely store and scale the most complicated data workloads.

## Components

The project comprises of three components, which work together to support document operations.

- **pg_documentdb_core :** PostgreSQL extension introducing BSON datatype support and operations for native Postgres.
- **pg_documentdb :** The public API surface for DocumentDB providing CRUD functionality on documents in the store.
- **pg_documentdb_gw :** The gateway protocol translation layer that converts the user's MongoDB APIs into PostgreSQL queries.

## Why DocumentDB ?

At DocumentDB, we believe in the power of open-source to drive innovation and collaboration. Our commitment to being a fully open-source MongoDB compatible document database means that we are dedicated to transparency, community involvement, and continuous improvement. We are open-sourced under the most permissive [MIT](https://opensource.org/license/mit) license, where developers and organizations alike have no restrictions incorporating the project into new and existing solutions of their own. DocumentDB introduces the BSON data type to PostgreSQL and provides APIs for seamless operation within native PostgreSQL, enhancing efficiency and aligning with operational advantages.

DocumentDB also provides a powerful on-premise solution, allowing organizations to maintain full control over their data and infrastructure. This flexibility ensures that you can deploy it in your own environment, meeting your specific security, compliance, and performance requirements. With DocumentDB, you get the best of both worlds: the innovation of open-source and the control of on-premise deployment.

### Based on Postgres

We chose PostgreSQL as our platform for several reasons:

1. **Proven Stability and Performance**: PostgreSQL has a long history of stability and performance, making it a trusted choice for mission-critical applications.
2. **Extensibility**: The extensible architecture of PostgreSQL allows us to integrate a DocumentDB API on BSON data type seamlessly, providing the flexibility to handle both relational and document data.
3. **Active Community**: PostgreSQL has a vibrant and active community that continuously contributes to its development, ensuring that it remains at the forefront of database technology.
4. **Advanced Features**: PostgreSQL offers a rich feature set, including advanced indexing, full-text search, and powerful querying capabilities, which enhance the functionality of DocumentDB.
5. **Compliance and Security**: PostgreSQL's robust security features and compliance with various standards makes it an ideal choice for organizations with stringent security and regulatory requirements.

## Get Started

[Building From Source](/docs/v1/building.md)

### Prerequisites
- Python 3.7+
- pip package manager
- Docker
- Git (for cloning the repository)

Step 1: Install Python

```bash

pip install pymongo

```

Step 2. Install optional dependencies

```bash

pip install dnspython

```

Step 3. Setup DocumentDB using Docker

```bash

   # Pull the latest DocumentDB Docker image
   docker pull ghcr.io/documentdb/documentdb/documentdb-local:latest

   # Tag the image for convenience
   docker tag ghcr.io/documentdb/documentdb/documentdb-local:latest documentdb

   # Run the container with your chosen username and password
   docker run -dt -p 10260:10260 --name documentdb-container documentdb --username <YOUR_USERNAME> --password <YOUR_PASSWORD>
   docker image rm -f ghcr.io/documentdb/documentdb/documentdb-local:latest || echo "No existing documentdb image to remove"

```

   > **Note:** Replace `<YOUR_USERNAME>` and `<YOUR_PASSWORD>` with your desired credentials. You must set these when creating the container for authentication to work.
   > 
   > **Port Note:** Port `10260` is used by default in these instructions to avoid conflicts with other local database services. You can use port `27017` (the standard MongoDB port) or any other available port if you prefer. If you do, be sure to update the port number in both your `docker run` command and your connection string accordingly.

Step 4: Initialize the pymongo client with the credentials from the previous step

```python

import pymongo

from pymongo import MongoClient

# Create a MongoDB client and open a connection to DocumentDB
client = pymongo.MongoClient(
    'mongodb://<YOUR_USERNAME>:<YOUR_PASSWORD>@localhost:10260/?tls=true&tlsAllowInvalidCertificates=true'
)

```

Step 5: Create a database and collection

```python

quickStartDatabase = client["quickStartDatabase"]
quickStartCollection = quickStartDatabase.create_collection("quickStartCollection")

```

Step 6: Insert documents

```python

# Insert a single document
quickStartCollection.insert_one({
       'name': 'John Doe',
       'email': 'john@email.com',
       'address': '123 Main St, Anytown, USA',
       'phone': '555-1234'
   })

# Insert multiple documents
quickStartCollection.insert_many([
    {
        'name': 'Jane Smith',
        'email': 'jane@email.com',
        'address': '456 Elm St, Othertown, USA',
        'phone': '555-5678'
    },
    {
        'name': 'Alice Johnson',
        'email': 'alice@email.com',
        'address': '789 Oak St, Sometown, USA',
        'phone': '555-8765'
    }
])

```

Step 7: Read documents

```python

# Read all documents
for document in quickStartCollection.find():
    print(document)

# Read a specific document
singleDocumentReadResult = quickStartCollection.find_one({'name': 'John Doe'})
print(singleDocumentReadResult)

```

Step 8: Run aggregation pipeline operation

```python

pipeline = [
    {'$match': {'name': 'Alice Johnson'}},
    {'$project': {
        '_id': 0,
        'name': 1,
        'email': 1
    }}
]

results = quickStartCollection.aggregate(pipeline)
print("Aggregation results:")
for eachDocument in results:
    print(eachDocument)

```

### Helpful Links

- Check out our [website](https://documentdb.io) to stay up to date with the latest on the project.
- Check out our [docs](https://documentdb.io/docs) for MongoDB API compatibility, quickstarts and more.
- Contributors and users can join the [DocumentDB Discord channel](https://discord.gg/vH7bYu524D) for quick collaboration.
- Check out [FerretDB](https://github.com/FerretDB/FerretDB) and their integration of DocumentDB as a backend engine.
