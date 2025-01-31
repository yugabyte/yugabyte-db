--
-- CREATE_TABLE
--

-- Datatypes
CREATE TABLE datatype_table (
    id             SERIAL,
    id_big         BIGSERIAL,
    is_small       SMALLSERIAL,
    v_bytea        BYTEA,
    v_smallint     SMALLINT,
    v_int          INT,
    v_bigint       BIGINT,
    v_char         CHAR(1),
    v_varchar      VARCHAR(10),
    v_text         TEXT,
    v_bool         BOOLEAN,
    v_inet         INET,
    v_cidr         CIDR,
    v_macaddr      MACADDR,
    v_numeric      NUMERIC(1,0),
    v_real         REAL,
    v_float        FLOAT(1),
    v_float8       FLOAT8,
    v_money        MONEY,
    v_tsquery      TSQUERY,
    v_tsvector     TSVECTOR,
    v_date         DATE,
    v_time         TIME,
    v_time_tz      TIME WITH TIME ZONE,
    v_timestamp    TIMESTAMP,
    v_timestamp_tz TIMESTAMP WITH TIME ZONE,
    v_interval     INTERVAL,
    v_bit          BIT,
    v_bit4         BIT(4),
    v_varbit       VARBIT,
    v_varbit4      VARBIT(4),
    v_box          BOX,
    v_circle       CIRCLE,
    v_lseg         LSEG,
    v_path         PATH,
    v_point        POINT,
    v_polygon      POLYGON,
    v_json         JSON,
    v_xml          XML,
    v_uuid         UUID,
    v_pg_snapshot  pg_snapshot,
    v_enum         ENUM_TEST,
    v_postal_code  japanese_postal_code,
    v_int2range    int2range,
    PRIMARY KEY (id),
    UNIQUE (id_big)
);

-- Constraint definitions

CREATE TABLE IF NOT EXISTS fkey_table (
    id           INT NOT NULL DEFAULT nextval('fkey_table_seq'::REGCLASS),
    datatype_id  INT NOT NULL REFERENCES datatype_table(id),
    big_id       BIGINT NOT NULL,
    sometext     TEXT COLLATE "POSIX",
    check_col_1  INT NOT NULL CHECK(check_col_1 < 10),
    check_col_2  INT NOT NULL,
    PRIMARY KEY  (id),
    CONSTRAINT fkey_big_id
      FOREIGN KEY (big_id)
      REFERENCES datatype_table(id_big),
    EXCLUDE USING btree (check_col_2 WITH =)
);

-- Typed table

CREATE TABLE employees OF employee_type (
    PRIMARY KEY (name),
    salary WITH OPTIONS DEFAULT 1000
);

-- Inheritance
CREATE TABLE person (
    id          INT NOT NULL PRIMARY KEY,
	name 		text,
	age			int4,
	location 	point
);

CREATE TABLE emp (
	salary 		int4,
	manager 	name
) INHERITS (person);


CREATE TABLE student (
	gpa 		float8
) INHERITS (person);

CREATE TABLE stud_emp (
	percent 	int4
) INHERITS (emp, student);


-- Storage parameters

CREATE TABLE storage (
    id INT
) WITH (
    fillfactor = 10,
    autovacuum_enabled = FALSE
);

-- LIKE

CREATE TABLE like_datatype_table (
  LIKE datatype_table
  EXCLUDING ALL
);

CREATE TABLE like_fkey_table (
  LIKE fkey_table
  INCLUDING DEFAULTS
  INCLUDING INDEXES
  INCLUDING STORAGE
);


-- Volatile table types
CREATE UNLOGGED TABLE unlogged_table (
    id INT PRIMARY KEY
);

CREATE TEMP TABLE temp_table (
    id INT PRIMARY KEY
);

CREATE TEMP TABLE temp_table_commit_delete (
    id INT PRIMARY KEY
)
ON COMMIT DELETE ROWS;

CREATE TEMP TABLE temp_table_commit_drop (
    id INT PRIMARY KEY
)
ON COMMIT DROP;
