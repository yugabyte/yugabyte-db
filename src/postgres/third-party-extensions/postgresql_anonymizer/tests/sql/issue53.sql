
CREATE EXTENSION anon CASCADE;

SELECT anon.start_dynamic_masking();

DROP TABLE if EXISTS companies CASCADE;
CREATE TABLE companies (
name               varchar(150),
email              varchar(150)
);

DROP TABLE IF EXISTS users CASCADE;
CREATE TABLE users
(
userid             serial         NOT NULL,
name               varchar(150),
encpassword        varchar(300),
saltkey            varchar(300),
isadminyesno       smallint,
datecreated        timestamp      DEFAULT (now())::timestamp without time zone,
datemodified       timestamp,
datedeleted        timestamp,
--guid               uuid           DEFAULT uuid_generate_v1() NOT NULL,
email              varchar(150),
masterencpassword  varchar(300),
isactive           smallint,
masterpassword     varchar(500)
);

-- Column userid is associated with sequence public.users_userid_seq
ALTER TABLE users
ADD CONSTRAINT users_pkey
PRIMARY KEY (userid);

COMMENT ON COLUMN users.email IS 'MASKED WITH FUNCTION anon.partial(email,2,$$******$$,2)::varchar(150)';
GRANT UPDATE, REFERENCES, DELETE, TRIGGER, TRUNCATE, INSERT, SELECT ON users TO admin;

COMMENT ON COLUMN users.name IS ' MASKED WITH FUNCTION anon.random_last_name()::varchar(150)';

COMMENT ON COLUMN companies.name IS ' MASKED WITH FUNCTION anon.random_last_name()::varchar(150)';
