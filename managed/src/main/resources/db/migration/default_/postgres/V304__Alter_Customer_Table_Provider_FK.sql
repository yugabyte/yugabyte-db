-- Copyright (c) YugaByte, Inc.

-- create foreign key from customer -> provider.
DO
$$ 
  BEGIN
    -- Check if the foreign key constraint already exists
    IF NOT EXISTS (SELECT conname FROM pg_constraint WHERE conname = 'fk_customer_provider_uuid') THEN
      -- Create the foreign key constraint
      ALTER TABLE provider
      ADD CONSTRAINT fk_customer_provider_uuid
      FOREIGN KEY (customer_uuid)
      REFERENCES customer (uuid)
      ON DELETE CASCADE
      ON UPDATE CASCADE;
    END IF;
  END 
$$;