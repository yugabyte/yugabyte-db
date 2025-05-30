# Introduction

`DocumentDB` is the engine powering vCore-based Azure Cosmos DB for MongoDB. It offers a native implementation of document-oriented NoSQL database, enabling seamless CRUD operations on BSON data types within a PostgreSQL framework. Beyond basic operations, DocumentDB empowers you to execute complex workloads, including full-text searches, geospatial queries, and vector embeddings on your dataset, delivering robust functionality and flexibility for diverse data management needs.

[PostgreSQL](https://www.postgresql.org/about/) is a powerful, open source object-relational database system that uses and extends the SQL language combined with many features that safely store and scale the most complicated data workloads.

## Components

The project comprises of two primary components, which work together to support document operations.

- **pg_documentdb_core :** PostgreSQL extension introducing BSON datatype support and operations for native Postgres.
- **pg_documentdb :** The public API surface for DocumentDB providing CRUD functionality on documents in the store.


## Why DocumentDB ?

At DocumentDB, we believe in the power of open-source to drive innovation and collaboration. Our commitment to being a fully open-source document database means that we are dedicated to transparency, community involvement, and continuous improvement. We are open-sourced under the most permissive [MIT](https://opensource.org/license/mit) license, where developers and organizations alike have no restrictions incorporating the project into new and existing solutions of their own. DocumentDB introduces the BSON data type and provides APIs for seamless operation within native PostgreSQL, enhancing efficiency and aligning with operational advantages.

DocumentDB also provides a powerful on-premise solution, allowing organizations to maintain full control over their data and infrastructure. This flexibility ensures that you can deploy it in your own environment, meeting your specific security, compliance, and performance requirements. With DocumentDB, you get the best of both worlds: the innovation of open-source and the control of on-premise deployment.

### Based on Postgres

DocumentDB is built on top of PostgreSQL, one of the most advanced and reliable open-source relational database systems available. We chose PostgreSQL as our base layer for several reasons:

1. **Proven Stability and Performance**: PostgreSQL has a long history of stability and performance, making it a trusted choice for mission-critical applications.
2. **Extensibility**: Their extensible architecture allows us to integrate a DocumentDB API on BSON data type seamlessly, providing the flexibility to handle both relational and document data.
3. **Active Community**: PostgreSQL has a vibrant and active community that continuously contributes to its development, ensuring that it remains at the forefront of database technology.
4. **Advanced Features**: PostgreSQL offers a rich set of features, including advanced indexing, full-text search, and powerful querying capabilities, which enhance the functionality of DocumentDB.
5. **Compliance and Security**: PostgreSQL's robust security features and compliance with various standards make it an ideal choice for organizations with stringent security and regulatory requirements.

By building on PostgreSQL, DocumentDB leverages these strengths to provide a powerful, flexible, and reliable document database that meets the need of modern applications. DocumentDB will continue to benefit from the advancements brought into the PostgreSQL ecosystem.

## Get Started

### Pre-requisite

- Ensure [Docker](https://docs.docker.com/engine/install/) is installed on your system.

### Building DocumentDB with Docker

Step 1: Clone the DocumentDB repo.

```bash
git clone https://github.com/microsoft/documentdb.git
```

Step 2: Create the docker image. Navigate to cloned repo.

```bash
docker build . -f .devcontainer/Dockerfile -t documentdb 
```

Note: Validate using `docker image ls`

Step 3: Run the Image as a container

```bash
docker run -v $(pwd):/home/documentdb/code -it documentdb /bin/bash 

cd code
```

(Aligns local location with docker image created, allows de-duplicating cloning repo again within image).<br>
Note: Validate container is running `docker container ls`

Step 4: Build & Deploy the binaries

```bash
make 
```

Note: Run in case of an unsuccessful build `git config --global --add safe.directory /home/DocumentDB/code` within image.

```bash
sudo make install
```

Note: To run backend postgresql tests after installing you can run `make check`.

You are all set to work with DocumentDB.

### Connecting to the Server

Step 1: Run `start_oss_server.sh` to initialize the DocumentDB server and manage dependencies.

```bash
./scripts/start_oss_server.sh
```

Step 2: Connect to `psql` shell

```bash
psql -p 9712 -d postgres
```

## Usage

Once you have your `DocumentDB` set up running, you can start with creating collections, indexes and perform queries on them.

### Create a collection

DocumentDB provides [documentdb_api.create_collection](https://github.com/microsoft/documentdb/wiki/Functions#create_collection) function to create a new collection within a specified database, enabling you to manage and organize your BSON documents effectively.

```sql
SELECT documentdb_api.create_collection('documentdb','patient');
```

### Perform CRUD operations

#### Insert documents

The [documentdb_api.insert_one](https://github.com/microsoft/documentdb/wiki/Functions#insert_one) command is used to add a single document into a collection.

```sql
select documentdb_api.insert_one('documentdb','patient', '{ "patient_id": "P001", "name": "Alice Smith", "age": 30, "phone_number": "555-0123", "registration_year": "2023","conditions": ["Diabetes", "Hypertension"]}');
select documentdb_api.insert_one('documentdb','patient', '{ "patient_id": "P002", "name": "Bob Johnson", "age": 45, "phone_number": "555-0456", "registration_year": "2023", "conditions": ["Asthma"]}');
select documentdb_api.insert_one('documentdb','patient', '{ "patient_id": "P003", "name": "Charlie Brown", "age": 29, "phone_number": "555-0789", "registration_year": "2024", "conditions": ["Allergy", "Anemia"]}');
select documentdb_api.insert_one('documentdb','patient', '{ "patient_id": "P004", "name": "Diana Prince", "age": 40, "phone_number": "555-0987", "registration_year": "2024", "conditions": ["Migraine"]}');
select documentdb_api.insert_one('documentdb','patient', '{ "patient_id": "P005", "name": "Edward Norton", "age": 55, "phone_number": "555-1111", "registration_year": "2025", "conditions": ["Hypertension", "Heart Disease"]}');
```

#### Read document from a collection

The `documentdb_api.collection` function is used for retrieving the documents in a collection.

```sql
SELECT document FROM documentdb_api.collection('documentdb','patient');
```

Alternatively, we can apply filter to our queries.

```sql
SET search_path TO documentdb_api, documentdb_core;
SET documentdb_core.bsonUseEJson TO true;

SELECT cursorPage FROM documentdb_api.find_cursor_first_page('documentdb', '{ "find" : "patient", "filter" : {"patient_id":"P005"}}');
```

We can perform range queries as well.

```sql
SELECT cursorPage FROM documentdb_api.find_cursor_first_page('documentdb', '{ "find" : "patient", "filter" : { "$and": [{ "age": { "$gte": 10 } },{ "age": { "$lte": 35 } }] }}');
```

#### Update document in a collection

DocumentDB uses the [documentdb_api.update](https://github.com/microsoft/documentdb/wiki/Functions#update) function to modify existing documents within a collection.

The SQL command updates the `age` for patient `P004`.

```sql
select documentdb_api.update('documentdb', '{"update":"patient", "updates":[{"q":{"patient_id":"P004"},"u":{"$set":{"age":14}}}]}');
```

Similarly, we can update multiple documents using `multi` property.

```sql
SELECT documentdb_api.update('documentdb', '{"update":"patient", "updates":[{"q":{},"u":{"$set":{"age":24}},"multi":true}]}');
```

#### Delete document from the collection

DocumentDB uses the [documentdb_api.delete](https://github.com/microsoft/documentdb/wiki/Functions#delete) function for precise document removal based on specified criteria.

The SQL command deletes the document for patient `P002`.

```sql
SELECT documentdb_api.delete('documentdb', '{"delete": "patient", "deletes": [{"q": {"patient_id": "P002"}, "limit": 1}]}');
```

### Collection management

We can review for the available collections and databases by querying [documentdb_api.list_collections_cursor_first_page](https://github.com/microsoft/documentdb/wiki/Functions#list_collections_cursor_first_page).

```sql
SELECT * FROM documentdb_api.list_collections_cursor_first_page('documentdb', '{ "listCollections": 1 }');
```

[documentdb_api.list_indexes_cursor_first_page](https://github.com/microsoft/documentdb/wiki/Functions#list_indexes_cursor_first_page) allows reviewing for the existing indexes on a collection. We can find collection_id from `documentdb_api.list_collections_cursor_first_page`.

```sql
SELECT documentdb_api.list_indexes_cursor_first_page('documentdb','{"listIndexes": "patient"}');
```

`ttl` indexes by default gets scheduled through the `pg_cron` scheduler, which could be reviewed by querying the `cron.job` table.

```sql
select * from cron.job;
```

### Indexing

#### Create an Index

DocumentDB uses the `documentdb_api.create_indexes_background` function, which allows background index creation without disrupting database operations.

The SQL command demonstrates how to create a `single field` index on `age` on the `patient` collection of the `documentdb`.

```sql
SELECT * FROM documentdb_api.create_indexes_background('documentdb', '{ "createIndexes": "patient", "indexes": [{ "key": {"age": 1},"name": "idx_age"}]}');
```

The SQL command demonstrates how to create a `compound index` on fields age and registration_year on the `patient` collection of the `documentdb`.

```sql
SELECT * FROM documentdb_api.create_indexes_background('documentdb', '{ "createIndexes": "patient", "indexes": [{ "key": {"registration_year": 1, "age": 1},"name": "idx_regyr_age"}]}');
```

#### Drop an Index

DocumentDB uses the `documentdb_api.drop_indexes` function, which allows you to remove an existing index from a collection. The SQL command demonstrates how to drop the index named `id_ab_1` from the `first_collection` collection of the `documentdb`.

```sql
CALL documentdb_api.drop_indexes('documentdb', '{"dropIndexes": "patient", "index":"idx_age"}');
```

### Perform aggregations `Group by`

DocumentDB provides the [documentdb_api.aggregate_cursor_first_page](https://github.com/microsoft/documentdb/wiki/Functions#aggregate_cursor_first_page) function, for performing aggregations over the document store.

The example projects an aggregation on number of patients registered over the years.

```sql
SELECT cursorpage FROM documentdb_api.aggregate_cursor_first_page('documentdb', '{ "aggregate": "patient", "pipeline": [ { "$group": { "_id": "$registration_year", "count_patients": { "$count": {} } } } ] , "cursor": { "batchSize": 3 } }');
```

We can perform more complex operations, listing below a few more usage examples.
The example demonstrates an aggregation on patients, categorizing them into buckets defined by registration_year boundaries.

```sql
SELECT cursorpage FROM documentdb_api.aggregate_cursor_first_page('documentdb', '{ "aggregate": "patient", "pipeline": [ { "$bucket": { "groupBy": "$registration_year", "boundaries": ["2022","2023","2024"], "default": "unknown" } } ], "cursor": { "batchSize": 3 } }');
```

This query performs an aggregation on the `patient` collection to group documents by `registration_year`. It collects unique patient conditions for each registration year using the `$addToSet` operator.

```sql
SELECT cursorpage FROM documentdb_api.aggregate_cursor_first_page('documentdb', '{ "aggregate": "patient", "pipeline": [ { "$group": { "_id": "$registration_year", "conditions": { "$addToSet": { "conditions" : "$conditions" } } } } ], "cursor": { "batchSize": 3 } }');
```

### Join data from multiple collections

Let's create an additional collection named `appointment` to demonstrate how a join operation can be performed.

```sql
select documentdb_api.insert_one('documentdb','appointment', '{"appointment_id": "A001", "patient_id": "P001", "doctor_name": "Dr. Milind", "appointment_date": "2023-01-20", "reason": "Routine checkup" }');
select documentdb_api.insert_one('documentdb','appointment', '{"appointment_id": "A002", "patient_id": "P001", "doctor_name": "Dr. Moore", "appointment_date": "2023-02-10", "reason": "Follow-up"}');
select documentdb_api.insert_one('documentdb','appointment', '{"appointment_id": "A004", "patient_id": "P003", "doctor_name": "Dr. Smith", "appointment_date": "2024-03-12", "reason": "Allergy consultation"}');
select documentdb_api.insert_one('documentdb','appointment', '{"appointment_id": "A005", "patient_id": "P004", "doctor_name": "Dr. Moore", "appointment_date": "2024-04-15", "reason": "Migraine treatment"}');
select documentdb_api.insert_one('documentdb','appointment', '{"appointment_id": "A007","patient_id": "P001", "doctor_name": "Dr. Milind", "appointment_date": "2024-06-05", "reason": "Blood test"}');
select documentdb_api.insert_one('documentdb','appointment', '{ "appointment_id": "A009", "patient_id": "P003", "doctor_name": "Dr. Smith","appointment_date": "2025-01-20", "reason": "Follow-up visit"}');
```

The example presents each patient along with the doctors visited.

```sql
SELECT cursorpage FROM documentdb_api.aggregate_cursor_first_page('documentdb', '{ "aggregate": "patient", "pipeline": [ { "$lookup": { "from": "appointment","localField": "patient_id", "foreignField": "patient_id", "as": "appointment" } },{"$unwind":"$appointment"},{"$project":{"_id":0,"name":1,"appointment.doctor_name":1,"appointment.appointment_date":1}} ], "cursor": { "batchSize": 3 } }');
```

### Community

- Please refer to page for contributing to our [Roadmap list](https://github.com/orgs/microsoft/projects/1407/views/1).
- [FerretDB](https://github.com/FerretDB/FerretDB) integration allows using DocumentDB as backend engine.

Contributors and users can join the [DocumentDB Discord channel in the Microsoft OSS server](https://aka.ms/documentdb_discord) for quick collaboration.

### FAQs

Q1. While performing `make check` if you encounter error `FATAL:  "/home/documentdb/code/pg_documentdb_core/src/test/regress/tmp/data" has wrong ownership`?

Please drop the `/home/documentdb/code/pg_documentdb_core/src/test/regress/tmp/` directory and rerun the `make check`.
