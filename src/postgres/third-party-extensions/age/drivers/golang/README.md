# age AGType parser and driver support for Golang

AGType parser and driver support for [Apache AGE](https://age.apache.org/), graph extension for PostgreSQL.

### Features
* Unmarshal AGE result data(AGType) to Vertex, Edge, Path
* Cypher query support for 3rd. Party sql driver (enables to use cypher queries directly)

### Prerequisites (Required)
* Java 11, or greater, installed.
* Go 1.18, or greater, installed.
* This module runs on the golang standard api [database/sql](https://golang.org/pkg/database/sql/) and [antlr4-python3](https://github.com/antlr/antlr4/tree/master/runtime/Go/antlr)
* ANTLR 4.11.1, or greater, installed.

### Installation (From source)
Run (Windows): install.bat
Run (Linux & OSX):
```
cd age/drivers/golang
./install.sh
```

### Go get
```
go get github.com/apache/age/drivers/golang
```
### gomod
```
require  github.com/apache/age/drivers/golang {version}
```

Check [latest version](https://github.com/apache/age/releases)

### For more information about [Apache AGE](https://age.apache.org/)
* Apache Age : https://age.apache.org/
* GitHub : https://github.com/apache/age
* Document : https://age.apache.org/docs/

### Check that Apache AGE is loaded on your PostgreSQL database
Connect to your containerized Postgres instance and then run the following commands:
```(sql)
# psql
CREATE EXTENSION age;
LOAD 'age';
SET search_path = ag_catalog, "$user", public;
```

### Test
For the tests to work, please make sure the following has been done.
* The driver has been successfully installed following the instructions above.
* Make sure the database you will run tests against has Apache AGE installed.
* Make sure to modify the DSN value in age/drivers/golang/age/age_test.go
* Make sure the DSN has the correct database and connect string.
* Make sure the database has a graph called testGraph created.

```
cd age/drivers/golang/age
go test . -v

```

### Samples
* Usage 1: using database/sql API and Cypher execution function 'ExecCypher'
  Sample : [samples/sql_api_sample.go](samples/sql_api_sample.go)

* Usage 2: using Age Wrapper 
  Sample : [samples/age_wrapper_sample.go](samples/age_wrapper_sample.go)

* Run Samples : [samples/main.go](samples/main.go)


### License
Apache-2.0 License
