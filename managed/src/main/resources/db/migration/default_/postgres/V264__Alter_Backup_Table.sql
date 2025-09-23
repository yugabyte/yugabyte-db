-- Copyright (c) YugaByte, Inc.

-- Adding the has_kms_history column to backup table.
ALTER TABLE backup ADD COLUMN if not exists has_kms_history boolean default false;
UPDATE backup SET has_kms_history = case
                            when has_kms_history = true then true
                            when coalesce(backup_info ->> 'kmsConfigUUID', 'false') != 'false' then true
                            else false
                                end;
