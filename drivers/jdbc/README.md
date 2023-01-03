# **AGE AGType parser and driver support for Java**

AGType parser and driver support for [Apache AGE](https://age.apache.org/), graph extension for PostgreSQL.

## Prerequisites

You should have installed following jar files and packages.

- [gradle](https://gradle.org/install/) build tool
- [postgres JDBC driver](https://jdbc.postgresql.org/download/)
- [antlr4-4.9.2-complete](https://repo1.maven.org/maven2/org/antlr/antlr4/4.9.2/)
- [common-lang3](http://www.java2s.com/Code/Jar/c/Downloadcommonlang3jar.htm)
- [commons-text-1.6](http://www.java2s.com/ref/jar/download-commonstext16jar-file.html)

Kindly unzip the jars if they are zipped before using them.

## Build from source

```bash
git clone https://github.com/apache/age.git
cd age/drivers/jdbc

gradle assemble
```

After the build completes successfully, a jar file will be created at path `age/drivers/jdbc/lib/build/libs/lib.jar`. Now add this JAR file to class path for your java project.

## Getting Started

* Install AGE on your machine. [https://age.apache.org/age-manual/master/index.html](https://age.apache.org/age-manual/master/index.html)
* Add the downloaded jar files to class path.
* Connect to the postgres server using pg JDBC drivers. 

Lets say we have a graph named `demo_graph` having some nodes. In order to extract its nodes we can try following steps. To create some more graphs. [https://github.com/apache/age#quick-start](https://github.com/apache/age#quick-start). Following sample code shows how to return query result as `Agtype`.

```java
import org.apache.age.jdbc.base.Agtype;
import org.postgresql.jdbc.PgConnection;

import java.sql.*;

public class Sample {
    static final String DB_URL = "jdbc:postgresql://localhost:5432/demo";
    static final String USER = "postgres";
    static final String PASS = "pass";

    public static void main(String[] args) {

        // Open a connection
        try {

            PgConnection connection = DriverManager.getConnection(DB_URL, USER, PASS).unwrap(PgConnection.class);
            connection.addDataType("agtype", Agtype.class);

            // configure AGE
            Statement stmt = connection.createStatement();
            stmt.execute("CREATE EXTENSION IF NOT EXISTS age;");
            stmt.execute("LOAD 'age'");
            stmt.execute("SET search_path = ag_catalog, \"$user\", public;");

            // Run cypher
            ResultSet rs = stmt.executeQuery("SELECT * from cypher('demo_graph', $$ MATCH (n) RETURN n $$) as (n agtype);");

            while (rs.next()) {

                // Returning Result as Agtype
                Agtype returnedAgtype = rs.getObject(1, Agtype.class);

                String nodeLabel = returnedAgtype.getMap().getObject("label").toString();
                String nodeProp =  returnedAgtype.getMap().getObject("properties").toString();

                System.out.println("Vertex : " + nodeLabel + ", \tProps : " + nodeProp);
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
```

Output

```
Vertex : Person, 	Props : {bornIn=Pakistan, name=imran}
Vertex : Person, 	Props : {bornIn=Pakistan, name=ali}
Vertex : Person, 	Props : {bornIn=US, name=james}
Vertex : Person, 	Props : {bornIn=Pakistan, name=ali}
Vertex : Person, 	Props : {bornIn=Pakistan, name=usama}
Vertex : Person, 	Props : {bornIn=Pakistan, name=akabr}
Vertex : Country, 	Props : {name=Pakistan}
Vertex : Country, 	Props : {name=US}
```

## For more information about [Apache AGE](https://age.apache.org/)

- Apache Age : [https://age.apache.org/](https://age.apache.org/)
- Github : [https://github.com/apache/age](https://github.com/apache/age)
- Document : [https://age.apache.org/age-manual/master/index.html](https://age.apache.org/age-manual/master/index.html)
