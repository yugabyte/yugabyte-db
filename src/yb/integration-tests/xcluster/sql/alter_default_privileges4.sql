-- Deletes the sequences row owned by sandeep and, by revoking the grant to
-- sandeep, returns the other sequences row to the built-in defaults, deleting
-- it too.  This is in its own file so that a missed deletion in
-- alter_default_privileges3.sql is caught before DROP OWNED could mask it.
DROP OWNED BY sandeep;
