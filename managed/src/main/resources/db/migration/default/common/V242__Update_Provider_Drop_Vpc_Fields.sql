-- Copyright (c) YugaByte, Inc.

-- Drop fields host_vpc_id, dest_vpc_id, host_vpc_region from the provider
-- as these will now be stored as part provider -> details -> cloudInfo -> aws/gcp
-- We can safely remove these fields without migrations as these are added
-- recently(V215) & has not been released to any customer. 
ALTER TABLE IF EXISTS provider DROP COLUMN IF EXISTS host_vpc_id;
ALTER TABLE IF EXISTS provider DROP COLUMN IF EXISTS dest_vpc_id;
ALTER TABLE IF EXISTS provider DROP COLUMN IF EXISTS host_vpc_region;
