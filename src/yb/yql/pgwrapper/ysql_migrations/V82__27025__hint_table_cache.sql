-- Update pg_hint_plan to version 1.5.1-yb-1.0 if it exists
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_hint_plan') THEN
        ALTER EXTENSION pg_hint_plan UPDATE TO '1.5.1-yb-1.0';
    END IF;
END $$;
