/* Add Availability Zone Details Column */
ALTER TABLE IF EXISTS availability_zone ADD COLUMN IF NOT EXISTS details json_alias;
ALTER TABLE availability_zone ALTER COLUMN details TYPE bytea USING pgp_sym_encrypt(details::text, 'availability_zone::details');

/* Modify Region Details Column */
ALTER TABLE region ALTER COLUMN details TYPE bytea USING pgp_sym_encrypt(details::text, 'region::details');

/* Modify Provider Details Column */
ALTER TABLE provider ALTER COLUMN details TYPE bytea USING pgp_sym_encrypt(details::text, 'provider::details');
