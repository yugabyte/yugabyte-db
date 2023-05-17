---
title: MyBatis
headerTitle: Use an ORM
linkTitle: Use an ORM
description: MyBatis Persistence framework support for YugabyteDB
headcontent: MyBatis Persistence framework support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  stable:
    identifier: java-orm-mybatis
    parent: java-drivers
    weight: 700
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../hibernate/" class="nav-link">
      Hibernate ORM
    </a>
  </li>

  <li >
    <a href="../ebean/" class="nav-link ">
      Ebean ORM
    </a>
  </li>

  <li >
    <a href="../mybatis/" class="nav-link active">
      MyBatis
    </a>
  </li>

</ul>

[MyBatis](https://mybatis.org/mybatis-3/) is a Java persistence framework with support for custom SQL, stored procedures, and advanced object mapping. MyBatis eliminates the need for writing native JDBC code, manual results mapping, and setting of DB parameters. MyBatis provides simple XML- and annotation-based support for query-to-object mapping for retrieving database records.

YugabyteDB YSQL API has full compatibility with MyBatis for Data persistence in Java applications. This page provides details for building Java applications using MyBatis for connecting to a YugabyteDB database.

## CRUD operations

Learn the basic steps required for connecting to the YugabyteDB database using MyBatis framework. The full working application is documented on the [Java ORM example application](../../orms/java/ysql-mybatis/) page.

The following sections demonstrate how to perform common tasks required for Java application development using MyBatis persistence framework.

### Step 1: Add the MyBatis dependency to your Java Project

Use the following [Maven](https://maven.apache.org/guides/development/guide-building-maven.html) dependency in your project's `pom.xml` file:

```xml
<dependency>
    <groupId>org.mybatis</groupId>
    <artifactId>mybatis</artifactId>
    <version>3.5.9</version>
</dependency>
```

If you're using Gradle, add the following dependency to your `build.gradle` file:

```java
// https://mvnrepository.com/artifact/org.mybatis/mybatis
implementation 'org.mybatis:mybatis:3.5.9'
```

Note: MyBatis persistence framework can be used with the [YugabyteDB JDBC driver](../yugabyte-jdbc) and the [PostgreSQL JDBC Driver](../postgres-jdbc).

### Step 2: Implement the entity object

Create a file `User.java` in the base package of the java project. Add the attributes for the User object and associated setters and getters.

```java
public class User {

    private Long userId;

    private String firstName;

    private String lastName;

    private String email;

    // getters and setters
}
```

### Step 3: Create the MyBatis data mapper for User object

MyBatis framework uses [data mappers](https://mybatis.org/mybatis-3/sqlmap-xml.html). Data mapper XML files are used for configuring DMLs that will be performed against an entity. Generally mappers are used for defining insert, update, delete, and select statements, and they are referred to as mapped SQL statements.

Create the XML file `UserMapper.xml` in the resources folder of your Java project and copy the following content:

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<!DOCTYPE mapper PUBLIC "-//mybatis.org//DTD Mapper 3.0//EN" "http://mybatis.org/dtd/mybatis-3-mapper.dtd" >

<mapper namespace="mybatis.mapper.UserMapper">

	<insert id="save" useGeneratedKeys = "true" parameterType = "User">
        insert into users (email, first_name, last_name) values (#{email}, #{firstName}, #{lastName})
    </insert>
    
    <resultMap id="userResultMap" type="User">
        <id property="userId" column="user_id"/>
        <result property="email" column="email"/>
        <result property="firstName" column="first_name"/>
        <result property="lastName" column="last_ame"/>
    </resultMap>
    
    <select id="findById" resultMap="userResultMap">
        select * from users where user_id = #{userId}
    </select>
    
    <select id="findAll" resultMap="userResultMap" fetchSize="10" flushCache="false" useCache="false" timeout="60000" statementType="PREPARED" resultSetType="FORWARD_ONLY">
        select * from users
    </select>
    
    <delete id = "delete" parameterType = "User">
      delete from users where user_id = #{userId};
    </delete>
    
</mapper>
```

### Step 4: Configure the data mappers and datasource in MyBatis configuration file

All the data mappers must be defined in the MyBatis configuration file. Create `mybatis-config.xml` in the resources folder to configure the MyBatis framework. 

In `mybatis-config.xml`, define the User data mapper and the datasource for connecting to the YugabyteDB database.

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<!-- Mybatis config sample -->
<!DOCTYPE configuration
        PUBLIC "-//mybatis.org//DTD Config 3.0//EN" "http://mybatis.org/dtd/mybatis-3-config.dtd">
<configuration>
    <properties>
        <!-- enabling default property values -->
        <property name="org.apache.ibatis.parsing.PropertyParser.enable-default-value" value="true"/>
    </properties>
    <settings>
        <setting name="defaultFetchSize" value="100"/>
    </settings>
    <typeAliases>
        <typeAlias type="User" alias="User"/>
    </typeAliases>
    <environments default="development">
        <environment id="development">
            <transactionManager type="JDBC" />
            <dataSource type="POOLED">
                <property name="driver" value="com.yugabyte.Driver" />
                <property name="url" value="jdbc:yugabytedb://127.0.0.1:5433/yugabyte" />
                <!-- default property values support -->
                <property name="username" value="${db.username:yugabyte}" />
                <property name="password" value="${db.password:}" />
            </dataSource>
        </environment>
    </environments>
    <mappers>
        <mapper resource="UserMapper.xml" />
    </mappers>
</configuration>
```

### Step 5: Create MyBatis SQLSessionFactory object

[SQLSession](https://mybatis.org/mybatis-3/configuration.html#properties) provides methods for performing database operations, retrieving mappers and result set mapping, and so forth.

SQLSessionFactory is not thread safe, so you need a thread safe way of instantiating SQLSession. Create the `MyBatisUtil.java` class in the base package to implement a thread-safe way of creating the SQLSessionFactory.

```java

import java.io.IOException;
import java.io.InputStream;

import org.apache.ibatis.io.Resources;
import org.apache.ibatis.session.SqlSessionFactory;
import org.apache.ibatis.session.SqlSessionFactoryBuilder;

public class MybatisUtil {
	
	private static SqlSessionFactory sqlSessionFactory;
	
	public static SqlSessionFactory getSessionFactory() {
        String resource = "mybatis-config.xml";
        InputStream inputStream;
		try {
			inputStream = Resources.getResourceAsStream(resource);
			sqlSessionFactory =
			          new SqlSessionFactoryBuilder().build(inputStream);
		} catch (IOException e) {
			e.printStackTrace();
		}
		return sqlSessionFactory;
    }
}
```

### Step 5: Create a DAO object for User object

Create a Data Access Object (DAO) `UserDAO.java` in the base package. The DAO is used for implementing the basic CRUD operations for the domain object `User.java`.

Copy the following code into your project:

```java
import java.util.List;

import org.apache.ibatis.session.SqlSession;
import org.apache.ibatis.session.SqlSessionFactory;

public class UserDAO {

    private SqlSessionFactory sqlSessionFactory;

    public UserDAO(SqlSessionFactory sqlSessionFactory) {
        this.sqlSessionFactory = sqlSessionFactory;
    }

    public void save(final User entity) {
    	
        try (SqlSession session = sqlSessionFactory.openSession()) {
        	session.insert("mybatis.mapper.UserMapper.save", entity);
        	session.commit();
         } catch (RuntimeException rte) {} 
    }

    public User findById(final Long id) {
    	
    	User user = null;

        try (SqlSession session = sqlSessionFactory.openSession()) {
        	user =  session.selectOne("mybatis.mapper.UserMapper.findById", id);
        	
        } catch (RuntimeException rte) {} 
        
        return user;
    }

    public List<User> findAll() {
    	
    	List<User> users = null;
        try (SqlSession session = sqlSessionFactory.openSession()) {
            users = session.selectList("mybatis.mapper.UserMapper.findAll");
        } catch (RuntimeException rte) {}
    	
    	return users;
    }

    public void delete(final User user) {

        try (SqlSession session = sqlSessionFactory.openSession()) {
            session.delete("mybatis.mapper.UserMapper.delete", user.getUserId());
        } catch (RuntimeException rte) {}
    }

}
```

### Step 6: Query the YugabyteDB cluster using MyBatis Framework

Create a java class `MyBatisExample.java` in the base package of your project. The following sample code inserts a user record and queries the table content using MyBatis.

```java
import java.sql.SQLException;

import org.apache.ibatis.session.SqlSessionFactory;

public class MyBatisExample {
	
	  public static void main(String[] args) throws ClassNotFoundException, SQLException {

		  SqlSessionFactory sessionFactory = MybatisUtil.getSessionFactory();

		    System.out.println("Connected to the YugabyteDB Cluster successfully.");
			  UserDAO userDAO = new UserDAO(sessionFactory);
			  User user = new User();
			  user.setEmail("demo@yugabyte.com");
			  user.setFirstName("Alice");
			  user.setLastName("yugabeing");
			  
			  // Save an user
			  userDAO.save(user);
			  System.out.println("Inserted user record: " + user.getFirstName());

			  // Find the user
			  User userFromDB = userDAO.findById(new Long(201));
			  System.out.println("Query returned:" + userFromDB.toString());
		  }

}
```

When you run the Java project, `MyBatisExample.java` should output the following:

```txt
Connected to the YugabyteDB Cluster successfully.
Inserted user record: Alice
Query returned:User [userId=101, firstName=Alice, lastName=Yugabeing, email=demo@yugabyte.com]
```

## Learn more

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)