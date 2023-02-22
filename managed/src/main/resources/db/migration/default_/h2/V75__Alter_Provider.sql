ALTER TABLE provider ADD CONSTRAINT IF NOT EXISTS unique_name_per_cloud UNIQUE (customer_uuid, name, code);
