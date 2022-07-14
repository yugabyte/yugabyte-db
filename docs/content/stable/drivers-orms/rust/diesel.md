---
title: Diesel ORM
linkTitle: Diesel ORM
description: Diesel ORM support for YugabyteDB
headcontent: Diesel ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  stable:
    identifier: diesel-orm
    parent: rust-drivers
    weight: 600
type: docs
---

[Diesel](https://diesel.rs/) is a safe, extensible object-relational mapping (ORM) tool and query builder for [Rust](https://www.rust-lang.org/). Diesel lets you create safe and composable abstractions over queries, and eliminates the possibility of incorrect database interactions at compile time. It's designed to be abstracted over, enabling you to write reusable code and think in terms of your problem domain.

YugabyteDB's YSQL API is fully compatible with Diesel ORM for data persistence in Rust applications. This page provides details for getting started with Diesel ORM for connecting to YugabyteDB.

## Create a new Rust-Diesel project

Ensure you have a recent version of Rust and Cargo installed. Then, do the following:

1. Create a new project:

   ```shell
   $ cargo new --lib diesel_demo && cd diesel_demo
   ```

1. Add the following to the project's `Cargo.toml` file, in the `[dependencies]` section:

   ```toml
   diesel = { version = "1.4.4", features = ["postgres"] }
   dotenv = "0.15.0"
   ```

1. Install the Diesel command-line interface for PostgreSQL:

   ```shell
   $ cargo install diesel_cli --no-default-features --features postgres
   ```

1. Next, tell Diesel where to find your database. In the project's main directory, create a file called `.env` with the following content:

   ```env
   DATABASE_URL=postgres://yugabyte:yugabyte@localhost:5433/ysql_diesel
   ```

1. Execute the following command to finish setting up the project:

   ```shell
   $ diesel setup
   ```

   \
   This creates an empty migrations directory that you can use to manage your schema. It also creates the `ysql_diesel` database.

## Build the REST API

Migrations allow you to evolve the database schema over time. Each migration can be applied (via `up.sql`) or reverted (via `down.sql`).

1. Create a migration to create an employee table:

   ```shell
   $ diesel migration generate create_employee
   ```

   \
   This creates two empty files in the `migrations/create_employee migration` directory: `up.sql` and `down.sql`.

1. Add the following code in `up.sql`:

   ```sql
   CREATE TABLE employee(
   emp_id SERIAL PRIMARY KEY,
   firstname text,
   lastname text ,
   emp_email text
   );
   ```

1. Add the following code in `down.sql`:

   ```sql
   DROP TABLE employee;
   ```

   {{<note title="Note">}}

When you ran `diesel setup`, it created a file called `diesel.toml`. This file tells Diesel to create and maintain a file tracking your schema. After running the migrations, `src/schema.rs` gets populated with employee table information by the Diesel ORM as follows:

```rust
table! {
   employee (emp_id) {
       emp_id -> Int4,
       firstname -> Nullable<Text>,
       lastname -> Nullable<Text>,
       emp_email -> Nullable<Text>,
   }
}
```

   {{</note>}}

1. Create a file called `src/models.rs`, to contain the structure of what data must be queried for Employee and what data must be inserted for employee as a NewEmployee, and add the following code:

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

1. Add the following code to `src/lib.rs` to create a database connection and create a new employee:

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

1. Create the `src/bin` directory, and open it.

   ```shell
   $ mkdir src/bin
   $ cd src/bin
   ```

1. Create `insert_employee.rs` and add the following code:

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

   \
   This establishes the database connection, asks for details of the employee to be inserted, and inserts them into the employee table.

1. Create `show_employees.rs` and add the following code:

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

   \
   This prompts the user to enter the number of employees to display employees information according to the order of employee IDs.

## Run the APIs

1. Run the following command to insert the data:

   ```shell
   $ cargo run --bin insert_employee
   ```

1. Follow the prompts to enter the details of the employee to be inserted:

   ```output
   Enter the id of the new employee:
   1
   Enter the first name of the new employee:
   John
   Enter the last name of the new employee:
   Smith
   Enter the email of the new employee:
   jsmith@example.com

   --------

   Employee Inserted with id 1.
   ```

1. Run the following command to show the data:

   ```shell
   $ cargo run --bin  show_employees
   ```

   \
   This prompts you to enter number of employees to display. Enter `2` and get the first two employees' data:

   ```output
   Displaying details of first 2 employees according to id:

   1.
   Employee id: 1
   First Name: "John"
   Last Name: "Smith"
   Email: "jsmith@example.com"


   2.
   Employee id: 2
   First Name: "Tom"
   Last Name: "Stewart"
   Email: "tstewart@example.com"
   ```

## Next steps

- Explore [Scaling Rust Applications](/preview/explore/linear-scalability/) with YugabyteDB.
- Learn how to [develop Rust applications with Yugabyte Cloud](/preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-python/).
