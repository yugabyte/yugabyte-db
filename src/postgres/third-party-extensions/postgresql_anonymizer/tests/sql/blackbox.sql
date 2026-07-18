CREATE TABLE cluedo AS
  SELECT 'Colonel Mustard' AS name,'Candlestick' AS weapon, 'Kitchen' AS room;
SELECT pg_catalog.set_config('search_path', 'public', false);
CREATE EXTENSION anon CASCADE;
SELECT anon.init();
SECURITY LABEL FOR anon ON COLUMN cluedo.name
IS 'MASKED WITH VALUE NULL';
