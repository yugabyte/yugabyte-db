ALTER FUNCTION utl_file.fopen(text, text, text, integer, name) SECURITY INVOKER;
ALTER FUNCTION utl_file.fopen(text, text, text, integer) SECURITY INVOKER;
GRANT SELECT ON TABLE utl_file.utl_file_dir TO PUBLIC;
GRANT EXECUTE ON FUNCTION utl_file.tmpdir() TO PUBLIC;
