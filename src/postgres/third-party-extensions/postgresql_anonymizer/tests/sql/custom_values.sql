BEGIN;

CREATE EXTENSION anon;

SET anon.custom_values
TO '{ "url": "https://wikipedia.it", "city": "Roma", "postcode": "OOO42"}';


SELECT anon.custom_value('url') = 'https://wikipedia.it';

SELECT anon.custom_value('does_not_exist') IS NULL;

SELECT anon.custom_value('does_not_exist', 'default_value') = 'default_value';

ROLLBACK;
