CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE TABLE author (
	id SERIAL UNIQUE,
	name TEXT
);

INSERT INTO author VALUES (23457,'Stirling');

CREATE TABLE book (
	id SERIAL UNIQUE,
	name TEXT,
	fk_author_id INTEGER,
	FOREIGN KEY (fk_author_id) REFERENCES author(id)
);

INSERT INTO book
VALUES (1,'T2: Infiltrator',23457);


-- TEST 1 : randomize referenced key 
UPDATE author
SET id=anon.random_int_between(1,200);

-- TEST 2 : randomize foreign key 
UPDATE book
SET fk_author_id=anon.random_int_between(1,1000000);


DROP EXTENSION anon;
