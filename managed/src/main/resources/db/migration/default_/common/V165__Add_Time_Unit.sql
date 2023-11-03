-- Copyright (c) YugaByte, Inc.

ALTER TABLE backup ADD COLUMN IF NOT EXISTS expiry_time_unit varchar(50);
UPDATE backup SET expiry_time_unit = 'Days' WHERE expiry IS NOT NULL;
ALTER TABLE backup DROP CONSTRAINT IF EXISTS  ck_expiry_time_unit;
ALTER TABLE backup ADD CONSTRAINT ck_expiry_time_unit CHECK (expiry_time_unit in ('NanoSeconds','MicroSeconds','MilliSeconds','Seconds','Minutes','Hours', 'Days', 'Months', 'Years'));

ALTER TABLE schedule ADD COLUMN IF NOT EXISTS frequency_time_unit varchar(50) DEFAULT 'Minutes';
ALTER TABLE schedule DROP CONSTRAINT IF EXISTS  ck_frequency_time_unit;
ALTER TABLE schedule ADD CONSTRAINT ck_frequency_time_unit CHECK (frequency_time_unit in ('NanoSeconds','MicroSeconds','MilliSeconds','Seconds','Minutes','Hours', 'Days', 'Months', 'Years'));
