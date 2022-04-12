---
title: Diesel ORM
linkTitle: Diesel ORM
description: Diesel ORM support for YugabyteDB
headcontent: Diesel ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: diesel-orm
    parent: rust-drivers
    weight: 600
isTocNested: true
showAsideToc: true
---
[Diesel](https://diesel.rs/) is a safe, extensible object-relational mapping (ORM) tool and a query builder for Rust. It is the most productive way to interact with databases in Rust because of its safe and composable abstractions over queries. It eliminates the possibility of incorrect database interactions at compile time. It is designed to be abstracted over. It enables programmers to write reusable code and think in terms of your problem domain.

YugabyteDB YSQL API has full compatibility with Diesel ORM for Data persistence in Rust applications. This page provides details for getting started with Diesel ORM for connecting to YugabyteDB.

## Create a New Rust-Diesel Project 

1. First, Ensure you have Rust (1.58.1) and Cargo(1.58.0) installed. If not installed. 
2. Create a new project by using this:

```shell
$ cargo new --lib diesel_demo && cd diesel_demo
```
3. The project will have a `Cargo.toml` file. In that file, add the following dependency under `[dependencies]` for diesel with specific features for postgres  and .env (to manage environment variables) :

```toml
diesel = { version = "1.4.4", features = ["postgres"] }
dotenv = "0.15.0"
```
4. Install the diesel CLI for Postgres, on the system. To do that, run the command:

```shell
$ cargo install diesel_cli --no-default-features --features postgres
```
5. Now, to configure the YugabyteDB for the project, we need to tell Diesel where to find our database. We can do this by setting the `DATABASE_URL` environment variable. To set this environment variable, create a file in the main directory of the project as .env and add the following configuration in the file:

```env
DATABASE_URL=postgres://yugabyte:yugabyte@localhost:5433/ysql_diesel
```

6. Finally run: 
```shell
$ diesel setup
```
This will create an empty migrations directory that we can use to manage our schema and will create the database `ysql_diesel` if default db is not used.

## Build the REST API using Diesel ORM with YugabyteDB

1. We are creating an example for managing the employees information for this first we will need an employee table in our database. So, for that create a migration with the following command:
```shell
$ diesel migration generate create_employee
```
This will create two empty files in the migrations directory under create_employee migration as `up.sql` and `down.sql`.  

2. Migrations allow us to evolve the database schema over time, each migration can be applied ( via `up.sql`) or reverted (via `down.sql`).

 Add the following code in `up.sql`:

 ```sql
CREATE TABLE employee(
   emp_id SERIAL PRIMARY KEY,
   firstname text,
   lastname text ,
   emp_email text
);
 ```

And add the following in `down.sql`:
```sql
DROP TABLE employee;
```
When we ran diesel setup, a file called `diesel.toml` was created which tells Diesel to maintain a file at src as `schema.rs` and after running the migrations the file `src/schema.rs` gets populated with employee table information by Diesel ORM as follows:
```rs
table! {
   employee (emp_id) {
       emp_id -> Int4,
       firstname -> Nullable<Text>,
       lastname -> Nullable<Text>,
       emp_email -> Nullable<Text>,
   }
}
```
3. Now, create `src/models.rs`, this will contain the structure of what data must be queried for Employee and what data must be inserted for employee as a NewEmployee and add the following code:

```rs
use crate::schema::employee;
#[derive(Queryable)]
pub struct Employee {
   pub emp_id: i32,
   pub firstname: Option<String>,
   pub lastname: Option<String>,
   pub emp_email: Option<String>,
}
 
#[derive(Insertable)]
#[table_name = "employee"]
pub struct NewEmployee{
   pub emp_id: i32,
   pub firstname: String,
   pub lastname: String,
   pub emp_email: String,
}
```

4. Now, in your `src/lib.rs` file add the following code to create a database connection and to create a new employee:

```rs
#[macro_use]
extern crate diesel;
extern crate dotenv;
 
use diesel::prelude::*;
use diesel::pg::PgConnection;
use dotenv::dotenv;
use std::env;
 
pub mod schema;
pub mod models;
 
use self::models::{NewEmployee, Employee};
 
pub fn establish_connection() -> PgConnection {
   dotenv().ok();
 
   let database_url = env::var("DATABASE_URL")
       .expect("DATABASE_URL must be set");
   PgConnection::establish(&database_url)
       .expect(&format!("Error connecting to {}", database_url))
}
pub fn create_employee(conn: &PgConnection, emp_id: i32, firstname: String, lastname: String, emp_email: String) -> Employee {
   use schema::employee;
 
   let new_employee = NewEmployee {
       emp_id: emp_id,
       firstname: firstname,
       lastname: lastname,
       emp_email: emp_email
   };
 
   diesel::insert_into(employee::table)
       .values(&new_employee)
       .get_result(conn)
       .expect("Error saving new employee")
}
```

5. Create a directory first as `src/bin` which will contain main files for inserting data and retrieving the data from the database. First, we will insert some new employee for which we create the `insert_employee.rs` under `src/bin` and add the following code to it which will first establish the connection with database and then ask the details of the employee to be inserted and insert them into employee table via that connection:

```rs
extern crate diesel_demo;
extern crate diesel;
 
use self::diesel_demo::*;
use self::models::*;
use self::diesel::prelude::*;
use std::io;
 
fn main() {
   use diesel_demo::schema::employee::dsl::*;
 
   let connection = establish_connection();
 
   let mut input_empid: String= "".to_string();
   let mut input_firstname: String = "".to_string();
   let mut input_lastname: String = "".to_string();
   let mut input_email: String = "".to_string();
 
 
   println!("Enter the id of the new employee:");
   io::stdin().read_line(&mut input_empid).expect("failed to readline");
 
   let mut empid = input_empid.trim().parse::<i32>().unwrap();
 
   println!("Enter the first name of the new employee:");
   io::stdin().read_line(&mut input_firstname).expect("failed to readline");
   input_firstname = input_firstname.trim().to_string();
 
   println!("Enter the last name of the new employee:");
   io::stdin().read_line(&mut input_lastname).expect("failed to readline");
   input_lastname = input_lastname.trim().to_string();
 
   println!("Enter the email of the new employee:");
   io::stdin().read_line(&mut input_email).expect("failed to readline");
   input_email = input_email.trim().to_string();
 
 
   let employee_inserted=create_employee(&connection, empid, input_firstname, input_lastname, input_email);
 
   println!("\n--------\n");
   println!("Employee Inserted with id {}.", employee_inserted.emp_id);
}
```

6. Create a file under the same directory `src/bin` as `show_employees.rs` and add the following code in it which will first prompt us to enter number of employees to display employees information according to the order of employee ids-
```rs
extern crate diesel_demo;
extern crate diesel;
 
use self::diesel_demo::*;
use self::models::*;
use self::diesel::prelude::*;
use std::io;
 
fn main() {
   use diesel_demo::schema::employee::dsl::*;
 
   let connection = establish_connection();
 
   let totalemployees = employee
           .load::<Employee>(&connection)
           .expect("Error loading employees");
 
   if totalemployees.len() == 0{
       println!("\n No employees to display!! \n ");
       return;
   }
 
   let mut limit_of_employees:i32 = 10;
   let mut employeeslimit_string: String = "".to_string();
 
   println!("Enter the number of employees to show out of {} employees: " , totalemployees.len());
   io::stdin().read_line(&mut employeeslimit_string).expect("failed to readline");
   limit_of_employees=employeeslimit_string.trim().parse::<i32>().unwrap();
 
   let results = employee
           .order(emp_id)
           .limit(limit_of_employees.into())
           .load::<Employee>(&connection)
           .expect("Error loading employees");
 
   println!("\n Displaying details of first {} employees according to id: \n", results.len());
   let mut index: i32 =1;
   for employee_data in results {
       println!("{}.", index);
       println!("Employee id: {:?}", employee_data.emp_id);
       println!("First Name: {:?}", employee_data.firstname.unwrap());
       println!("Last Name: {:?}", employee_data.lastname.unwrap());
       println!("Email: {:?}", employee_data.emp_email.unwrap());
       println!("\n");
       index=index+1;
   }
}
```

## Running the APIs 

1. Run the following command to insert the data:
```shell
$ cargo run --bin insert_employee
```
This will prompt you to enter the details of the employee to be inserted as follows and enter the information and this data will be inserted-

```output
Enter the id of the new employee:
1
Enter the first name of the new employee:
John
Enter the last name of the new employee:
Smith
Enter the email of the new employee:
jsmith@xyz.com

--------

Employee Inserted with id 1.
```

2. Run the following command to show the data:
```shell
$ cargo run --bin  show_employees
```
This will prompt to enter number of employees to display so enter that and get those many employees details:

```output
Displaying details of first 2 employees according to id: 

1.
Employee id: 1
First Name: "John"
Last Name: "Smith"
Email: "jsmith@xyz.com"


2.
Employee id: 2
First Name: "Tom"
Last Name: "Stewart"
Email: "tstewart@xyz.com"

```

## Next Steps

- Explore [Scaling Rust Applications](/preview/explore/linear-scalability) with YugabyteDB.
- Learn how to [develop Rust applications with Yugabyte Cloud](/preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-python/).
