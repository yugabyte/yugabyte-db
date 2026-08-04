-- Add disabled column to pitr_config table with default value false
ALTER TABLE pitr_config ADD COLUMN disabled BOOLEAN NOT NULL DEFAULT false;
