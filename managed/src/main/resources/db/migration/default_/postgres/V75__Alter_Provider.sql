DO
$$
  BEGIN
    -- Look for our constraint
    IF NOT EXISTS (SELECT constraint_name
                   FROM information_schema.constraint_column_usage
                   WHERE constraint_name = 'unique_name_per_cloud') THEN
        EXECUTE 'ALTER TABLE provider ADD CONSTRAINT unique_name_per_cloud UNIQUE (customer_uuid, name, code);';
    END IF;
  END;
$$;