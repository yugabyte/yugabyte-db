BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.load();

-- running the test on pg_temp does not improve perf 
--SET search_path TO pg_temp, public; 

CREATE TABLE customer(
	id SERIAL,
	full_name TEXT
);

INSERT INTO customer(id, full_name)
SELECT generate_series(1,1000), 'X Y';


\timing

\echo 'test 0 : basic update'
UPDATE customer SET full_name='A B';

\echo 'test 1 : random_first_name()'
UPDATE customer                                                                                                                                        
SET                                                                                                                                                    
    full_name=anon.random_first_name()                                                                              
;      

\echo 'test 2 : random_last_name()'
UPDATE customer                                                                                                                                        
SET                                                                                                                                                    
    full_name=anon.random_last_name()                                                                              
;      

\echo 'test 3 : random_first_name() + random_last_name() '
UPDATE customer
SET 
	full_name=anon.random_first_name() || ' ' || anon.random_last_name()
;

\echo 'test 4 : random_first_name() + random_last_name() with UNLOGGED TABLE'
CREATE UNLOGGED TABLE fake_customer AS 
SELECT 
	id,
	full_name=anon.random_first_name() || ' ' || anon.random_last_name() 
FROM customer
;

ROLLBACK;

