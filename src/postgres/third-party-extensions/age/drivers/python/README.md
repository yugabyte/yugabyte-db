Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.

Apache AGE](https://age.apache.org/) is a PostgreSQL extension that provides graph database functionality. The goal of the Apache AGE project is to create single storage that can handle both relational and graph model data so that users can use standard ANSI SQL along with openCypher, the Graph query language. This repository hosts the development of the Python driver for this Apache extension (currently in Incubator status). Thanks for checking it out.

A graph consists of a set of vertices (also called nodes) and edges, where each individual vertex and edge possesses a map of properties. A vertex is the basic object of a graph, that can exist independently of everything else in the graph. An edge creates a directed connection between two vertices. A graph database is simply composed of vertices and edges. This type of database is useful when the meaning is in the relationships between the data. Relational databases can easily handle direct relationships, but indirect relationships are more difficult to deal with in relational databases. A graph database stores relationship information as a first-class entity. Apache AGE gives you the best of both worlds, simultaneously.

Apache AGE is:

- **Powerful** -- AGE adds graph database support to the already popular PostgreSQL database: PostgreSQL is used by organizations including Apple, Spotify, and NASA.
- **Flexible** -- AGE allows you to perform openCypher queries, which make complex queries much easier to write.
- **Intelligent** -- AGE allows you to perform graph queries that are the basis for many next level web services such as fraud & intrustion detection, master data management, product recommendations, identity and relationship management, experience personalization, knowledge management and more.

# AGE AGType parser and driver support for Python
AGType parser and driver support for [Apache AGE](https://age.apache.org/), graph extension for PostgreSQL.

### Features
* Unmarshal AGE result data(AGType) to Vertex, Edge, Path
* Cypher query support for Psycopg2 PostgreSQL driver (enables to use cypher queries directly)

### Prerequisites
* over Python 3.9
* This module runs on [psycopg2](https://www.psycopg.org/) and [antlr4-python3](https://pypi.org/project/antlr4-python3-runtime/)
```
sudo apt-get update
sudo apt-get install python3-dev libpq-dev
git clone https://github.com/apache/age.git
cd age/drivers/python
```

### Install required packages
```
pip install -r requirements.txt
```

### Test
```
python test_age_py.py \
-host "127.0.0.1" \
-db "postgres" \
-u "postgres" \
-pass "agens" \
-port 5432 \
-gn "test_graph"
```

```
python -m unittest -v test_agtypes.py
```

### Build from source
```
python setup.py install
```

### For more information about [Apache AGE](https://age.apache.org/)
* Apache Age : https://age.apache.org/
* GitHub : https://github.com/apache/age
* Document : https://age.apache.org/age-manual/master/index.html

### Check AGE loaded on your PostgreSQL
Connect to your containerized Postgres instance and then run the following commands:
```
# psql 
CREATE EXTENSION age;
LOAD 'age';
SET search_path = ag_catalog, "$user", public;
```

### Usage
* If you are familiar with Psycopg2 driver : Go to [Jupyter Notebook : Basic Sample](samples/apache-age-basic.ipynb) 
* Simpler way to access Apache AGE [AGE Sample](samples/apache-age-note.ipynb) in Samples.
* Agtype converting samples: [Agtype Sample](samples/apache-age-agtypes.ipynb) in Samples.

### Non-Superuser Usage
* For non-superuser usage see: [Allow Non-Superusers to Use Apache Age](https://age.apache.org/age-manual/master/intro/setup.html).
* Make sure to give your non-superuser db account proper permissions to the graph schemas and corresponding objects
* Make sure to initiate the Apache Age python driver with the ```load_from_plugins``` parameter. This parameter tries to
  load the Apache Age extension from the PostgreSQL plugins directory located at ```$libdir/plugins/age```. Example:
  ```python.
  ag = age.connect(host='localhost', port=5432, user='dbuser', password='strong_password', 
                   dbname=postgres, load_from_plugins=True, graph='graph_name)
  ```

### License
Apache-2.0 License


## Networkx
### Netowkx Unit test
```
python test_networkx.py \
-host "127.0.0.1" \
-db "postgres" \
-u "postgres" \
-pass "agens" \
-port 5432
```
Here the following value required
- `-host` : host name (optional)
- `-db` : database name
- `-u` : user name
- `-pass` : password
- `-port` : port (optional)

### Networkx to AGE
Insert From networkx directed graph into an Age database.
#### Parameters

- `connection` (psycopg2.connect): Connection object to the Age database.

- `G` (networkx.DiGraph): Networkx directed graph to be converted and inserted.

- `graphName` (str): Name of the age graph.

#### Returns

None

#### Example

```python

# Create a Networkx DiGraph
G = nx.DiGraph()
G.add_node(1)
G.add_node(2)
G.add_edge(1, 2)

# Convert and insert the graph into the Age database
graphName = "sample_graph"
networkx_to_age(connection, G, graphName)
```



### AGE to Netowkx

Converts data from a Apache AGE graph database into a Networkx directed graph.

#### Parameters

- `connection` (psycopg2.connect): Connection object to the PostgreSQL database.
- `graphName` (str): Name of the graph.
- `G` (None | nx.DiGraph): Optional Networkx directed graph. If provided, the data will be added to this graph.
- `query` (str | None): Optional Cypher query to retrieve data from the database.

#### Returns

- `nx.DiGraph`: Networkx directed graph containing the converted data.

#### Example

```python
# Call the function to convert data into a Networkx graph
graph = age_to_networkx(connection, graphName="MyGraph" )
```
