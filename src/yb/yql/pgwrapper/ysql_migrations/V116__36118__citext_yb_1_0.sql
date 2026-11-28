-- Update citext to version 1.6-yb-1.0 if it is installed at 1.6
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'citext' AND extversion = '1.6') THEN
        ALTER EXTENSION citext UPDATE TO '1.6-yb-1.0';
    END IF;
END $$;
