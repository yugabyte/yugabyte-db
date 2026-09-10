-- Copyright (c) YugabyteDB, Inc.

-- No cascade because the certificate is not deleted when the node agent is deleted.
ALTER TABLE node_agent ADD COLUMN certificate_uuid UUID NULL REFERENCES certificate_info (uuid);