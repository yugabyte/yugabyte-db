///
/// # Test fixtures
///
/// Create objects for testing purpose
///
/// This is a very basic testing context !
///
/// For more sophisticated use cases, use the `pg_regress` functional test suite
/// See the `make installcheck` target for more details
///
use pgrx::prelude::*;

// dead_code warnings are disabled because this mod is loaded in lib.rs
// in order to make it available to all the others mod test section,
// but `cargo pgrx run` can't see where these functions are used

#[allow(dead_code)]
pub fn create_masking_functions() -> pg_sys::Oid {
    Spi::run("
        CREATE SCHEMA outfit;
        CREATE FUNCTION outfit.mask(SMALLINT) RETURNS SMALLINT LANGUAGE SQL AS $$ SELECT 0::SMALLINT $$;
        CREATE FUNCTION outfit.mask(INT) RETURNS INT LANGUAGE SQL AS $$ SELECT 0 $$;
        CREATE FUNCTION outfit.mask(BIGINT) RETURNS BIGINT LANGUAGE SQL AS $$ SELECT 0::BIGINT $$;
        SECURITY LABEL FOR anon ON FUNCTION outfit.mask(SMALLINT) IS 'TRUSTED';
        SECURITY LABEL FOR anon ON FUNCTION outfit.mask(INT) IS 'TRUSTED';

        CREATE FUNCTION outfit.belt() RETURNS TEXT LANGUAGE SQL AS $$ SELECT 'x' $$;
        CREATE FUNCTION public.belt() RETURNS TEXT LANGUAGE SQL AS $$ SELECT 'x' $$;
        SECURITY LABEL FOR anon ON FUNCTION outfit.belt() IS 'UNTRUSTED';
        SECURITY LABEL FOR anon ON FUNCTION public.belt() IS 'TRUSTED';

        CREATE FUNCTION outfit.cape() RETURNS INT LANGUAGE SQL AS $$ SELECT 0 $$;

    ").unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'outfit'::REGNAMESPACE::OID;")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_restricted_function() -> pg_sys::Oid {
    Spi::run(
        "
        CREATE FUNCTION pseudo_id() RETURNS INT LANGUAGE SQL AS $$ SELECT 1 $$;
        SECURITY LABEL FOR anon ON FUNCTION pseudo_id() IS 'RESTRICTED';
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'pseudo_id()'::REGPROCEDURE::OID;")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_masked_role_in_policy(role: &str, policy: &str) -> pg_sys::Oid {
    Spi::run(
        format!(
            "
        CREATE ROLE {role};
        SECURITY LABEL FOR {policy} ON ROLE {role} is 'MASKED';
    "
        )
        .as_str(),
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>(
        format!(
            "
        SELECT '{role}'::REGROLE::OID;
    "
        )
        .as_str(),
    )
    .unwrap()
    .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_masked_role() -> pg_sys::Oid {
    Spi::run(
        "
        CREATE ROLE batman;
        SECURITY LABEL FOR anon ON ROLE batman is 'MASKED';
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'batman'::REGROLE::OID;")
        .unwrap()
        .expect("should be an OID")
}

// A table with a WHEN clause
#[allow(dead_code)]
pub fn create_table_account() -> pg_sys::Oid {
    Spi::run(
        "
        CREATE TABLE account
            AS SELECT
                'foo@bar.com' AS email,
                'foobar'      AS login,
                FALSE         AS is_admin
        ;
        SECURITY LABEL FOR anon ON COLUMN account.email
            IS 'MASKED WITH FUNCTION anon.fake_email()';

        SECURITY LABEL FOR anon ON TABLE account
            IS 'MASKED WHEN NOT is_admin';
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'account'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

// An unmasked table
#[allow(dead_code)]
pub fn create_table_call() -> pg_sys::Oid {
    Spi::run(
        "
         CREATE TABLE call AS
         SELECT  '410-719-9009'::TEXT        AS sender,
                 '410-258-4863'::TEXT        AS receiver,
                 '2004-07-08'::DATE          AS day,
                 1035::INT                   AS duration
         ;
         ALTER TABLE call DROP COLUMN duration;
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'call'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

// A masked table with quotes
#[allow(dead_code)]
pub fn create_table_user() -> pg_sys::Oid {
    Spi::run(
        "
        CREATE TABLE \"User\"
            AS SELECT
                'foo@bar.com' AS \"Email\",
                'foobar'      AS \"LoGiN\"
        ;
        SECURITY LABEL FOR anon ON COLUMN \"User\".\"Email\"
            IS 'MASKED WITH FUNCTION anon.fake_email()';
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT '\"User\"'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

// A masked table with a dropped column
#[allow(dead_code)]
pub fn create_table_person() -> pg_sys::Oid {
    Spi::run(
        "
        CREATE TABLE person AS
        SELECT
            'She/Her'                   AS pronouns,
            'Sarah'::VARCHAR(30)        AS firstname,
            'Connor'::TEXT              AS lastname
        ;

        ALTER TABLE person DROP COLUMN pronouns;

        SECURITY LABEL FOR anon ON COLUMN person.lastname
        IS 'MASKED WITH VALUE NULL';

        SECURITY LABEL FOR anon ON TABLE person
        IS 'TABLESAMPLE BERNOULLI(10)';
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'person'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_table_location() -> pg_sys::Oid {
    Spi::run(
        "
         CREATE SCHEMA \"Postal_Info\";
         CREATE TABLE \"Postal_Info\".location AS
         SELECT  '53540'::VARCHAR(5)        AS zipcode,
                 'Gotham'::TEXT             AS city
         ;
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT '\"Postal_Info\".location'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_table_with_defaults() -> pg_sys::Oid {
    Spi::connect_mut(|client| {
        client
            .update(
                "
            CREATE TABLE test_defaults (
                id INT PRIMARY KEY GENERATED ALWAYS AS IDENTITY,
                col_with_default TEXT DEFAULT 'default_value',
                col_with_complex_default TIMESTAMP DEFAULT now(),
                col_without_default INTEGER,
                col_generated NUMERIC GENERATED ALWAYS AS (col_without_default / 2.54) STORED,
                col_dropped TEXT DEFAULT NULL
            );
            ALTER TABLE test_defaults DROP COLUMN col_dropped;
        ",
                None,
                &[],
            )
            .unwrap();

        client
            .select("SELECT 'test_defaults'::REGCLASS::OID", None, &[])
            .unwrap()
            .first()
            .get_one::<pg_sys::Oid>()
            .unwrap()
    })
    .unwrap()
}

#[allow(dead_code)]
pub fn create_trusted_schema() -> pg_sys::Oid {
    Spi::run(
        "
        CREATE SCHEMA gotham;
        SECURITY LABEL FOR anon ON SCHEMA gotham is 'TRUSTED';
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'gotham'::REGNAMESPACE::OID;")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_unmasked_role() -> pg_sys::Oid {
    Spi::run(
        "
        CREATE ROLE bruce;
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'bruce'::REGROLE::OID;")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_untrusted_schema() -> pg_sys::Oid {
    Spi::run(
        "
        CREATE SCHEMA arkham;
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'arkham'::REGNAMESPACE::OID;")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn declare_masking_policies() {
    Spi::run(
        "
        SET anon.masking_policies = 'devtests, analytics';
    ",
    )
    .unwrap();
}

#[allow(dead_code)]
pub fn declare_sampling_for_database(db: String) {
    Spi::run(&format!(
        "
        SECURITY LABEL FOR anon ON DATABASE {db}
            IS 'TABLESAMPLE SYSTEM(33)';
    "
    ))
    .unwrap();
}

#[allow(dead_code)]
pub fn disable_static_masking() {
    Spi::run(
        "
        SET anon.static_masking TO off;
    ",
    )
    .unwrap();
}

#[allow(dead_code)]
pub fn enable_replica_masking() {
    Spi::run(
        "
        SET anon.replica_masking TO on;
    ",
    )
    .unwrap();
}

#[allow(dead_code)]
pub fn set_search_path(search_path: String) {
    Spi::run(
        format!(
            "
        SET search_path TO {search_path};
    "
        )
        .as_str(),
    )
    .unwrap();
}

#[allow(dead_code)]
pub fn trust_masking_functions_schema() {
    Spi::run(
        "
        SECURITY LABEL FOR anon ON SCHEMA outfit IS 'TRUSTED';
    ",
    )
    .unwrap();
}

// A masked parent table
#[allow(dead_code)]
pub fn create_table_orders_with_masked_column() -> pg_sys::Oid {
    Spi::run(
        "
        DROP TABLE IF EXISTS orders;
        CREATE TABLE orders AS
        SELECT
            1 as orders_id,
            1 as customer_id,
            1 as orders_details_id,
            'VAT_NUMBER'::TEXT AS vat_number
        ;

        SECURITY LABEL FOR anon ON COLUMN orders.vat_number
        IS 'MASKED WITH VALUE NULL';

        ALTER TABLE orders
        ADD CONSTRAINT pk_orders
        PRIMARY KEY (orders_id);
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'orders'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

// A masked child table
#[allow(dead_code)]
pub fn create_table_orders_details_with_masked_column() -> pg_sys::Oid {
    Spi::run(
        "
        DROP TABLE IF EXISTS orders_details;
        CREATE TABLE orders_details AS
        SELECT
            1 as line_number,
            1 as orders_id,
            2 as qt,
            'product description'::TEXT AS product_description
        ;

        ALTER TABLE orders_details
        ADD CONSTRAINT fk_parent_child
        FOREIGN KEY (orders_id)
        REFERENCES orders(orders_id);

        SECURITY LABEL FOR anon ON COLUMN orders_details.product_description
        IS 'MASKED WITH VALUE NULL';

    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'orders_details'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

// A non masked child table
#[allow(dead_code)]
pub fn create_table_orders_details_without_masked_column() -> pg_sys::Oid {
    Spi::run(
        "
        DROP TABLE IF EXISTS orders_details;
        CREATE TABLE orders_details AS
        SELECT
            1 as line_number,
            1 as orders_id,
            2 as qt,
            'product description'::TEXT AS product_description
        ;
        ALTER TABLE orders_details
        ADD CONSTRAINT fk_parent_child
        FOREIGN KEY (orders_id)
        REFERENCES orders(orders_id);
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'orders_details'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_table_employee_with_masked_column() -> pg_sys::Oid {
    Spi::run(
        "
        DROP TABLE IF EXISTS employee;
        CREATE TABLE employee (
        id INT PRIMARY KEY,
        firstname TEXT,
        lastname TEXT,
        phone TEXT UNIQUE,
        ssn TEXT,
        iban TEXT UNIQUE,
        linkedin_url TEXT
        );

        INSERT INTO employee
        VALUES
        (1,'Sarah','Connor','0609110911','153-473-999','FI9562411724264125',NULL),
        (2,'Kyle', 'Reese', '0230366642','573-731-129','GB32BARC20039593595589',NULL)
        ;

        SECURITY LABEL FOR anon ON COLUMN employee.phone
        IS 'MASKED WITH FUNCTION pg_catalog.md5(phone)';
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'employee'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_table_call_history_with_masked_column() -> pg_sys::Oid {
    Spi::run(
        "
        DROP TABLE IF EXISTS call_history;
        CREATE TABLE call_history (
        id INT,
        fk_employee_phone TEXT REFERENCES employee(phone) DEFERRABLE,
        call_start TIMESTAMP,
        call_stop TIMESTAMP
        );

        INSERT INTO call_history
        VALUES
        (464,'0609110911','2020-05-24 08:11:30', '2020-05-24 08:11:43'),
        (465,'0609110911','2020-05-25 15:58:23', '2020-05-24 16:01:11');

        INSERT INTO call_history
        VALUES
        (464,'0609110911','2020-05-24 08:11:30', '2020-05-24 08:11:43'),
        (465,'0609110911','2020-05-25 15:58:23', '2020-05-24 16:01:11');

        SECURITY LABEL FOR anon ON COLUMN call_history.fk_employee_phone
        IS 'MASKED WITH FUNCTION pg_catalog.md5(fk_employee_phone)';

    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'call_history'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_table_call_history_without_fk_with_masked_column() -> pg_sys::Oid {
    Spi::run(
        "
        DROP TABLE IF EXISTS call_history;
        CREATE TABLE call_history (
        id INT,
        employee_phone TEXT,
        call_start TIMESTAMP,
        call_stop TIMESTAMP
        );

        INSERT INTO call_history
        VALUES
        (464,'0609110911','2020-05-24 08:11:30', '2020-05-24 08:11:43'),
        (465,'0609110911','2020-05-25 15:58:23', '2020-05-24 16:01:11');

        INSERT INTO call_history
        VALUES
        (464,'0609110911','2020-05-24 08:11:30', '2020-05-24 08:11:43'),
        (465,'0609110911','2020-05-25 15:58:23', '2020-05-24 16:01:11');

        SECURITY LABEL FOR anon ON COLUMN call_history.employee_phone
        IS 'MASKED WITH FUNCTION pg_catalog.md5(employee_phone)';

    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'call_history'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_table_employee_with_self_join_and_masked_column() -> pg_sys::Oid {
    Spi::run(
        "
        DROP TABLE IF EXISTS employee;

        CREATE TABLE employee (
            employee_id INT PRIMARY KEY,
            firstname TEXT,
            lastname TEXT,
            phone TEXT UNIQUE,
            ssn TEXT,
            manager_id INT REFERENCES employee(employee_id)
        );

        INSERT INTO employee(employee_id, firstname, lastname, phone, ssn, manager_id)
        VALUES
        (1,'Sarah','Connor','0609110911','153-473-999',3),
        (2,'Kyle', 'Reese', '0230366642','573-731-129',3),
        (3,'Boss', 'The', '0230366643','573-731-113',NULL)
        ;

        SECURITY LABEL FOR anon ON COLUMN employee.phone
        IS 'MASKED WITH FUNCTION pg_catalog.md5(phone)';

    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'employee'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_4_linked_tables_and_masked_column() -> pg_sys::Oid {
    Spi::run(
        "
        DROP TABLE IF EXISTS order_items;
        DROP TABLE IF EXISTS orders;
        DROP TABLE IF EXISTS products;
        DROP TABLE IF EXISTS customers;

        -- Table 1: customers
        CREATE TABLE customers (
            customer_id SERIAL PRIMARY KEY,
            first_name VARCHAR(50) NOT NULL,
            last_name VARCHAR(50) NOT NULL,
            email VARCHAR(100) UNIQUE
        );

        -- Table 2: products
        CREATE TABLE products (
            product_id SERIAL PRIMARY KEY,
            product_name VARCHAR(100) NOT NULL,
            price NUMERIC(10, 2) NOT NULL CHECK (price >= 0)
        );

        -- Table 3: orders
        -- This table links to the customers table.
        CREATE TABLE orders (
            order_id SERIAL PRIMARY KEY,
            customer_id INT NOT NULL,
            order_date TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (customer_id) REFERENCES customers(customer_id)
        );

        -- Table 4: order_items
        -- This is a child table that links to both orders and products.
        CREATE TABLE order_items (
            order_item_id SERIAL PRIMARY KEY,
            order_id INT NOT NULL,
            product_id INT NOT NULL,
            quantity INT NOT NULL CHECK (quantity > 0),
            FOREIGN KEY (order_id) REFERENCES orders(order_id),
            FOREIGN KEY (product_id) REFERENCES products(product_id)
        );
        -- Masking rules
        SECURITY LABEL FOR anon ON FUNCTION pg_catalog.round(numeric,integer) IS 'TRUSTED';
        SECURITY LABEL FOR anon ON FUNCTION pg_catalog.abs(numeric) IS 'TRUSTED';

        SECURITY LABEL FOR anon ON COLUMN customers.email
            IS 'MASKED WITH FUNCTION pg_catalog.md5(email)';

        SECURITY LABEL FOR anon ON COLUMN products.price
            IS 'MASKED WITH FUNCTION pg_catalog.round(price, 2)';

        SECURITY LABEL FOR anon ON COLUMN orders.order_date
            IS 'MASKED WITH FUNCTION pg_catalog.to_char(order_date, ''YYYY-MM-DD'')';

        SECURITY LABEL FOR anon ON COLUMN order_items.quantity
            IS 'MASKED WITH FUNCTION pg_catalog.abs(quantity)';
    ",
    )
    .unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 'customers'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn parse_select_query(query_string: &str) -> PgBox<pg_sys::Query> {
    use crate::compat;
    use crate::masking;
    use std::ffi::CString;

    // Create a CString from the query
    let query_cstr =
        CString::new(query_string).expect("Failed to create CString from SELECT query");

    // Get the first statement (should be a single SELECT statement)
    let raw_stmt = masking::parse_subquery(query_string.to_string());

    // Transform the SelectStmt into a Query using PostgreSQL's query planner
    let mut numparams: i32 = 0;
    let query = unsafe {
        compat::parse_analyze_varparams(
            raw_stmt.as_ptr(),
            query_cstr.as_ptr(),
            std::ptr::null_mut(),              // no parameter types
            std::ptr::addr_of_mut!(numparams), // no parameter
            std::ptr::null_mut(),              // no environment
        )
    };

    unsafe { PgBox::from_pg(query) }
}

#[allow(dead_code)]
pub fn create_10_tables_with_fk_masked() -> pg_sys::Oid {
    Spi::run("
        DROP TABLE IF EXISTS t, t1, t2, t3, t4;

        CREATE TABLE t1(i int PRIMARY KEY, t text);
        CREATE TABLE t2(j int PRIMARY KEY, i int, t text, CONSTRAINT fk FOREIGN KEY (i) REFERENCES T1 (i));
        CREATE TABLE t3(k int PRIMARY KEY, j int, t text, CONSTRAINT fk FOREIGN KEY (j) REFERENCES T2 (j));
        CREATE TABLE t4(l int PRIMARY KEY, k int, t text, CONSTRAINT fk FOREIGN KEY (k) REFERENCES T3 (k));
        CREATE TABLE t(m int, t text);

        DROP TABLE IF EXISTS u, u1, u2, u3, u4;
        CREATE TABLE u1(i int PRIMARY KEY, t text);
        CREATE TABLE u2(j int PRIMARY KEY, i int, t text, CONSTRAINT fk FOREIGN KEY (i) REFERENCES U1 (i));
        CREATE TABLE u3(k int PRIMARY KEY, j int, t text, CONSTRAINT fk FOREIGN KEY (j) REFERENCES U2 (j));
        CREATE TABLE u4(l int PRIMARY KEY, k int, t text, CONSTRAINT fk FOREIGN KEY (k) REFERENCES U3 (k));
        CREATE TABLE u(m int, t text);

        SECURITY LABEL FOR anon ON COLUMN t.t  IS 'MASKED WITH VALUE NULL';
        SECURITY LABEL FOR anon ON COLUMN t1.i IS 'MASKED WITH FUNCTION pg_catalog.md5(i)';
        SECURITY LABEL FOR anon ON COLUMN t2.i IS 'MASKED WITH FUNCTION pg_catalog.md5(i)';
        SECURITY LABEL FOR anon ON COLUMN t2.j IS 'MASKED WITH FUNCTION pg_catalog.md5(j)';
        SECURITY LABEL FOR anon ON COLUMN t3.k IS 'MASKED WITH FUNCTION pg_catalog.md5(k)';
        SECURITY LABEL FOR anon ON COLUMN t3.j IS 'MASKED WITH FUNCTION pg_catalog.md5(j)';
        SECURITY LABEL FOR anon ON COLUMN t4.k IS 'MASKED WITH FUNCTION pg_catalog.md5(k)';


        SECURITY LABEL FOR anon ON COLUMN u.t  IS 'MASKED WITH VALUE NULL';
        SECURITY LABEL FOR anon ON COLUMN u1.i IS 'MASKED WITH FUNCTION pg_catalog.md5(i)';
        SECURITY LABEL FOR anon ON COLUMN u2.i IS 'MASKED WITH FUNCTION pg_catalog.md5(i)';
        SECURITY LABEL FOR anon ON COLUMN u2.j IS 'MASKED WITH FUNCTION pg_catalog.md5(j)';
        SECURITY LABEL FOR anon ON COLUMN u3.k IS 'MASKED WITH FUNCTION pg_catalog.md5(k)';
        SECURITY LABEL FOR anon ON COLUMN u3.j IS 'MASKED WITH FUNCTION pg_catalog.md5(j)';
        SECURITY LABEL FOR anon ON COLUMN u4.k IS 'MASKED WITH FUNCTION pg_catalog.md5(k)';


        INSERT INTO t(m) SELECT generate_series(1, 100);
        INSERT INTO t1(i) SELECT generate_series(1, 110);
        INSERT INTO t3(k) SELECT generate_series(1, 110);

        INSERT INTO u(m) SELECT generate_series(1, 100);
        INSERT INTO u1(i) SELECT generate_series(1, 110);
        INSERT INTO u3(k) SELECT generate_series(1, 110);

        analyze;
    ",).unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 't1'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_5_tables_with_fk_not_masked() -> pg_sys::Oid {
    Spi::run("

        DROP TABLE IF EXISTS t, t1, t2, t3, t4;

        CREATE TABLE t1(i int PRIMARY KEY, t text);
        CREATE TABLE t2(j int PRIMARY KEY, i int, t text, CONSTRAINT fk FOREIGN KEY (i) REFERENCES T1 (i));
        CREATE TABLE t3(k int PRIMARY KEY, j int, t text, CONSTRAINT fk FOREIGN KEY (j) REFERENCES T2 (j));
        CREATE TABLE t4(l int PRIMARY KEY, k int, t text, CONSTRAINT fk FOREIGN KEY (k) REFERENCES T3 (k));
        CREATE TABLE t(m int, t text);

        SECURITY LABEL FOR anon ON COLUMN t.t  IS 'MASKED WITH VALUE NULL';
        SECURITY LABEL FOR anon ON COLUMN t1.t IS 'MASKED WITH VALUE NULL';
        SECURITY LABEL FOR anon ON COLUMN t2.t IS 'MASKED WITH VALUE NULL';
        SECURITY LABEL FOR anon ON COLUMN t3.t IS 'MASKED WITH VALUE NULL';
        SECURITY LABEL FOR anon ON COLUMN t4.t IS 'MASKED WITH VALUE NULL';

        INSERT INTO t(m) SELECT generate_series(1, 100);
        INSERT INTO t1(i) SELECT generate_series(1, 110);
        INSERT INTO t3(k) SELECT generate_series(1, 110);

        analyze;
    ",).unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 't1'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn create_5_tables_with_fk_not_anon_but_with_cycling_fk() -> pg_sys::Oid {
    Spi::run("
        DROP TABLE IF EXISTS t1 CASCADE;
        DROP TABLE IF EXISTS t2 CASCADE;
        DROP TABLE IF EXISTS t3 CASCADE;
        DROP TABLE IF EXISTS t4 CASCADE;
        DROP TABLE IF EXISTS t5 CASCADE;

        -- 1. Create Table 1 (T1)
        -- Its foreign key (t5_ref_id) must be added later.
        CREATE TABLE t1 (
            t1_id SERIAL PRIMARY KEY,
            t1_data VARCHAR(100) NOT NULL,
            t5_ref_id INTEGER -- Will hold the foreign key reference to Table_T5
        );

        -- 2. Create Table 2 (T2) - References T1
        CREATE TABLE t2 (
            t2_id SERIAL PRIMARY KEY,
            t2_data VARCHAR(100) NOT NULL,
            t1_ref_id INTEGER NOT NULL,
            FOREIGN KEY (t1_ref_id) REFERENCES t1(t1_id)
        );

        -- 3. Create Table 3 (T3) - References T2
        CREATE TABLE t3 (
            t3_id SERIAL PRIMARY KEY,
            t3_data VARCHAR(100) NOT NULL,
            t2_ref_id INTEGER NOT NULL,
            FOREIGN KEY (t2_ref_id) REFERENCES t2(t2_id)
        );

        -- 4. Create Table 4 (T4) - References T3 AND T1 (THE NEW LINK)
        CREATE TABLE t4 (
            t4_id SERIAL PRIMARY KEY,
            t4_data VARCHAR(100) NOT NULL,
            t3_ref_id INTEGER NOT NULL,
            -- New Foreign Key linking T4 directly back to T1
            t1_ref_id INTEGER NOT NULL,
            FOREIGN KEY (t3_ref_id) REFERENCES t3(t3_id),
            FOREIGN KEY (t1_ref_id) REFERENCES t1(t1_id)
        );

        -- 5. Create Table 5 (T5) - References T4
        CREATE TABLE t5 (
            t5_id SERIAL PRIMARY KEY,
            t5_data VARCHAR(100) NOT NULL,
            t4_ref_id INTEGER NOT NULL,
            FOREIGN KEY (t4_ref_id) REFERENCES t4(t4_id)
        );

        -- 6. Add the Final Link to Complete the Cycle
        -- This closes the main T1 <- ... <- T5 <- T1 loop.
        ALTER TABLE t1
        ADD CONSTRAINT fk_t1_t5_ref
        FOREIGN KEY (t5_ref_id)
        REFERENCES t5(t5_id);

        -- Sample Data Insertion (multiple rows, respecting FK constraints and cycle)
        -- Insert 3 sets of related rows

        -- Set 1
        INSERT INTO t1 (t1_data) VALUES ('T1 row 1') RETURNING t1_id; -- Assume t1_id=1
        INSERT INTO t2 (t2_data, t1_ref_id) VALUES ('T2 row 1', 1) RETURNING t2_id; -- t2_id=1
        INSERT INTO t3 (t3_data, t2_ref_id) VALUES ('T3 row 1', 1) RETURNING t3_id; -- t3_id=1
        INSERT INTO t4 (t4_data, t3_ref_id, t1_ref_id) VALUES ('T4 row 1', 1, 1) RETURNING t4_id; -- t4_id=1
        INSERT INTO t5 (t5_data, t4_ref_id) VALUES ('T5 row 1', 1); -- t5_id=1
        UPDATE t1 SET t5_ref_id = 1 WHERE t1_id = 1;

        -- Set 2
        INSERT INTO t1 (t1_data, t5_ref_id) VALUES ('T1 row 2', NULL) RETURNING t1_id; -- t1_id=2
        INSERT INTO t2 (t2_data, t1_ref_id) VALUES ('T2 row 2', 2) RETURNING t2_id; -- t2_id=2
        INSERT INTO t3 (t3_data, t2_ref_id) VALUES ('T3 row 2', 2) RETURNING t3_id; -- t3_id=2
        INSERT INTO t4 (t4_data, t3_ref_id, t1_ref_id) VALUES ('T4 row 2', 2, 2) RETURNING t4_id; -- t4_id=2
        INSERT INTO t5 (t5_data, t4_ref_id) VALUES ('T5 row 2', 2) RETURNING t5_id; -- t5_id=2
        UPDATE t1 SET t5_ref_id = 2 WHERE t1_id = 2;

        -- Set 3
        INSERT INTO t1 (t1_data, t5_ref_id) VALUES ('T1 row 3', NULL) RETURNING t1_id; -- t1_id=3
        INSERT INTO t2 (t2_data, t1_ref_id) VALUES ('T2 row 3', 3) RETURNING t2_id; -- t2_id=3
        INSERT INTO t3 (t3_data, t2_ref_id) VALUES ('T3 row 3', 3) RETURNING t3_id; -- t3_id=3
        INSERT INTO t4 (t4_data, t3_ref_id, t1_ref_id) VALUES ('T4 row 3', 3, 3) RETURNING t4_id; -- t4_id=3
        INSERT INTO t5 (t5_data, t4_ref_id) VALUES ('T5 row 3', 3) RETURNING t5_id; -- t5_id=3
        UPDATE t1 SET t5_ref_id = 3 WHERE t1_id = 3;

        SECURITY LABEL FOR anon ON COLUMN t1.t1_data
        IS 'MASKED WITH FUNCTION anon.partial(t1_data,1,$$**$$,1)';
        SECURITY LABEL FOR anon ON COLUMN t2.t2_data
        IS 'MASKED WITH FUNCTION anon.partial(t2_data,1,$$**$$,1)';
        SECURITY LABEL FOR anon ON COLUMN t3.t3_data
        IS 'MASKED WITH FUNCTION anon.partial(t3_data,1,$$**$$,1)';
        SECURITY LABEL FOR anon ON COLUMN t4.t4_data
        IS 'MASKED WITH FUNCTION anon.partial(t4_data,1,$$**$$,1)';
        SECURITY LABEL FOR anon ON COLUMN t5.t5_data
        IS 'MASKED WITH FUNCTION anon.partial(t5_data,1,$$**$$,1)';

    ",).unwrap();
    Spi::get_one::<pg_sys::Oid>("SELECT 't1'::REGCLASS::OID")
        .unwrap()
        .expect("should be an OID")
}

#[allow(dead_code)]
pub fn clean_tables() {
    Spi::run(
        "

        DROP TABLE IF EXISTS t, t1, t2, t3, t4 CASCADE;
        DROP TABLE IF EXISTS u, u1, u2, u3, u4 CASCADE;
        DROP TABLE IF EXISTS order_items CASCADE;
        DROP TABLE IF EXISTS orders CASCADE;
        DROP TABLE IF EXISTS products CASCADE;
        DROP TABLE IF EXISTS customers CASCADE;
        DROP TABLE IF EXISTS employee CASCADE;
        DROP TABLE IF EXISTS call_history CASCADE;
        DROP TABLE IF EXISTS orders_details CASCADE;
        DROP TABLE IF EXISTS test_defaults;
        DROP TABLE IF EXISTS \"User\";
        DROP TABLE IF EXISTS person;
    ",
    )
    .unwrap();
}
