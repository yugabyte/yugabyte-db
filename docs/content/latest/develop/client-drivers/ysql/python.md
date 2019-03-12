
## Installation

Install the python PostgreSQL driver using the following command. You can get further details for the driver [here](https://pypi.org/project/psycopg2/).

```sh
$ pip install psycopg2-binary
```

## Working Example

### Pre-requisites

This tutorial assumes that you have:

- installed YugaByte DB and created a universe with YSQL enabled. If not, please follow these steps in the [quick start guide](../../../quick-start/explore-ysql/).


### Writing the python code

Create a file `yb-sql-helloworld.py` and add the following content to it.

```python
import psycopg2

# Create the database connection.                                                                 
conn = psycopg2.connect("host=127.0.0.1 port=5433 dbname=postgres user=postgres password=postgres")

# Open a cursor to perform database operations                                                    
cur = conn.cursor()

# Create the table.                                                                               
cur.execute(
  """                                                                                             
  CREATE TABLE employee (id int PRIMARY KEY,                                                      
                         name varchar,                                                            
                         age int,                                                                 
                         language varchar);                                                       
  """)
print "Created table employee"

# Insert a row.                                                                                   
cur.execute("INSERT INTO employee (id, name, age, language) VALUES (%s, %s, %s, %s)",
            (1, 'John', 35, 'Python'))
print "Inserted (id, name, age, language) = (1, 'John', 35, 'Python')"

# Query the row.                                                                                  
cur.execute("SELECT name, age, language FROM employee WHERE id = 1;")
row = cur.fetchone()
print "Query returned: %s, %s, %s" % (row[0], row[1], row[2])

# Close the connection.                                                                           
cur.close()
conn.close()
```

### Running the application

To run the application, type the following:

```sh
$ python yb-sql-helloworld.py
```

You should see the following output.

```
Created table employee
Inserted (id, name, age, language) = (1, 'John', 35, 'Python')
Query returned: John, 35, Python
```
