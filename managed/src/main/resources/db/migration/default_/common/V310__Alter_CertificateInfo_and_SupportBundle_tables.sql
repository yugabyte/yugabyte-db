-- Copyright (c) YugaByte, Inc.

-- Delete invalid certs from certificate_info before creating the constraint.
DELETE FROM certificate_info WHERE customer_uuid NOT IN (SELECT uuid FROM customer);

-- Delete invalid bundles from support_bundle before creating the constraint.
DELETE FROM support_bundle WHERE scope_uuid NOT IN (SELECT universe_uuid FROM universe);

-- Add a foreign key constraint to certificate_info with ON DELETE CASCADE
ALTER TABLE certificate_info ADD CONSTRAINT fk_certificate_info_customer FOREIGN KEY (customer_uuid) REFERENCES customer (uuid) ON DELETE CASCADE;

-- Add a foreign key constraint to support_bundle with ON DELETE CASCADE
ALTER TABLE support_bundle ADD CONSTRAINT fk_sb_universe FOREIGN KEY (scope_uuid) REFERENCES universe (universe_uuid) ON DELETE CASCADE;

-- Change RESTRICT to CASCADE on fks: fk_image_bundle_provider_uuid, fk_region_provider_uuid, fk_availability_zone_region_uuid
ALTER TABLE image_bundle DROP CONSTRAINT fk_image_bundle_provider_uuid;
ALTER TABLE image_bundle ADD CONSTRAINT fk_image_bundle_provider_uuid FOREIGN KEY (provider_uuid) REFERENCES provider(uuid) ON UPDATE CASCADE ON DELETE CASCADE;

ALTER TABLE region DROP CONSTRAINT fk_region_provider_uuid;
ALTER TABLE region ADD CONSTRAINT fk_region_provider_uuid FOREIGN KEY (provider_uuid) REFERENCES provider(uuid) ON UPDATE CASCADE ON DELETE CASCADE;

ALTER TABLE availability_zone DROP CONSTRAINT fk_availability_zone_region_uuid;
ALTER TABLE availability_zone ADD CONSTRAINT fk_availability_zone_region_uuid FOREIGN KEY (region_uuid) REFERENCES region(uuid) ON UPDATE CASCADE ON DELETE CASCADE;
