BEGIN;

CREATE EXTENSION anon;

SET anon.custom_values
TO '{ "url": "https://wikipedia.it", "city": "Roma", "postcode": "OOO42"}';

SELECT anon.custom_value('url') = 'https://wikipedia.it';

SELECT anon.custom_value('does_not_exist') IS NULL;

SELECT anon.custom_value('does_not_exist', 'default_value') = 'default_value';

CREATE TABLE citizen (
  id INT,
  name TEXT,
  city TEXT
);

INSERT INTO citizen VALUES ( 1, 'Kane', 'Xanadu' );

SECURITY LABEL FOR anon ON column citizen.city
 IS $$ MASKED WITH FUNCTION anon.custom_value('city') $$;


CREATE USER alice;

SECURITY LABEL FOR anon ON ROLE alice is 'MASKED';

GRANT pg_read_all_data TO alice;

SET anon.transparent_dynamic_masking TO on;


SET ROLE alice;

SELECT city = 'Roma' FROM citizen;

SAVEPOINT before_error;

SELECT anon.custom_value('city');

ROLLBACK TO before_error;


ROLLBACK;
