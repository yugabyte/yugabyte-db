-- Make a copy of all the access keys to be on safer side.
-- TODO: We should delete this table after a few releases or customer admin can review
-- and delete it if they are comfortable

CREATE TABLE access_key_no_provider_bak AS SELECT * FROM access_key
    WHERE provider_uuid NOT IN (SELECT p.uuid FROM provider p);

-- Now delete any keys with danging provider reference
DELETE FROM access_key
    WHERE provider_uuid NOT IN (SELECT p.uuid FROM provider p);

-- At this point we should be confident of referential integrity
-- proceed with fk constraint
ALTER TABLE IF EXISTS access_key
    DROP CONSTRAINT IF EXISTS fk_access_key_provider_uuid;
ALTER TABLE IF EXISTS access_key
    ADD CONSTRAINT fk_access_key_provider_uuid
    FOREIGN KEY (provider_uuid) REFERENCES provider (uuid)
    ON DELETE CASCADE ON UPDATE CASCADE;
