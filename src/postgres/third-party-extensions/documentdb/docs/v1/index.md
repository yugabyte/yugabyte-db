# Welcome to DocumentDB

**DocumentDB** is a fully open-source document-oriented database engine, built on PostgreSQL.

It supports seamless CRUD operations on BSON data types, full-text search, geospatial queries, and vector embeddings — all within the robust PostgreSQL ecosystem.


## Getting Started with DocumentDB

A curated collection of guides to help you understand what DocumentDB is, why it matters, and how to get up and running—from initial setup to advanced document operations.

- [Introduction](v1/introduction.md)
- [Why DocumentDB?](v1/why-documentdb.md)
- [DocumentDB Gateway](v1/gateway.md)
- [Prebuild Image](v1/prebuild-image.md)
- [Getting Started](v1/get-started.md)
- [Usage (CRUD)](v1/usage.md)
- [Collection Management](v1/collection-management.md)
- [Indexing](v1/indexing.md)
- [Aggregation](v1/aggregation.md)
- [Joins](v1/joins.md)
- [Packaging](v1/packaging.md)
- [Data Initialization](v1/data-initialization.md)

---

## 🚀 Features

- Native PostgreSQL extension with BSON support
- Powerful CRUD and indexing capabilities
- Support for full-text search, geospatial data, and vector workloads
- Fully open-source under the [MIT License](https://opensource.org/license/mit)
- On-premises and cloud-ready deployment

---

## 🧱 Components

- `pg_documentdb_core` – PostgreSQL extension for BSON type and operations
- `pg_documentdb` – Public API layer enabling document-oriented access
- `pg_documentdb_gw` – Gateway for DocumentDB, providing a MongoDB interface

---

## 🐳 Quick Start with Docker
### Prebuild Image For DocumentDB
There are prebuild images available for different platforms. You can find the list of prebuild images [here](v1/prebuild-image.md).

To run the prebuild image, use the following command:
```bash
# example for Ubuntu 22.04, PostgreSQL 16, amd64
# Choose the image tag according to your configuration
docker run -dt mcr.microsoft.com/cosmosdb/ubuntu/documentdb-oss:22.04-PG16-AMD64-0.103.0
docker exec -it <container-id> bash  
```

### Prebuild Image For DocumentDB with Gateway
To run the prebuild image with the DocumentDB Gateway, use the following command:
```bash
docker run -dt -p 10260:10260 -e USERNAME=<username> -e PASSWORD=<password> ghcr.io/microsoft/documentdb/documentdb-local:latest

mongosh localhost:10260 -u <username> -p <password> \
        --authenticationMechanism SCRAM-SHA-256 \
        --tls \
        --tlsAllowInvalidCertificates
```

### Build DocumentDB from Source
```bash
git clone https://github.com/microsoft/documentdb.git
cd documentdb
docker build -f .devcontainer/Dockerfile -t documentdb .
docker run -v $(pwd):/home/documentdb/code -it documentdb /bin/bash
make && sudo make install
```

### Community

- Please refer to page for contributing to our [Roadmap list](https://github.com/orgs/microsoft/projects/1407/views/1).
- [FerretDB](https://github.com/FerretDB/FerretDB) integration allows using DocumentDB as backend engine.

Contributors and users can join the [DocumentDB Discord channel in the Microsoft OSS server](https://aka.ms/documentdb_discord) for quick collaboration.

### How to Contribute

To contribute, see these documents:

- [Code of Conduct](./CODE_OF_CONDUCT.md)  
- [Security](./SECURITY.md)  
- [Contributing](./CONTRIBUTING.md)

### FAQs

Q1. While performing `make check` if you encounter error `FATAL:  "/home/documentdb/code/pg_documentdb_core/src/test/regress/tmp/data" has wrong ownership`?

Please drop the `/home/documentdb/code/pg_documentdb_core/src/test/regress/tmp/` directory and rerun the `make check`.


Contributors and users can join the [DocumentDB Discord channel in the Microsoft OSS server](https://aka.ms/documentdb_discord) for quick collaboration.

### License

**DocumentDB** is licensed under the MIT License. See [LICENSE](./LICENSE.txt) for details.

### Trademarks

This project may use trademarks or logos. Use of Microsoft trademarks must follow Microsoft’s [Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks). Use of third-party marks is subject to their policies.
