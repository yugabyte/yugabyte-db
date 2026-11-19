
SET search_path='';

CREATE OR REPLACE FUNCTION anon.custom_value(
  key TEXT,
  def TEXT DEFAULT NULL
)
RETURNS TEXT
AS $$
  SELECT
    COALESCE (
      NULLIF(
        pg_catalog.current_setting('anon.custom_values'),''
      )::JSON ->> key,
      def
  );
$$
  LANGUAGE SQL
  PARALLEL SAFE
  STABLE
  SECURITY DEFINER -- required to read the settings
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.custom_value(TEXT,TEXT) IS 'RESTRICTED';

RESET search_path;
