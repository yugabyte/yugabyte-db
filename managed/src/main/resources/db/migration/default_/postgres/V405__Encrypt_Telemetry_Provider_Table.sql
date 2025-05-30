-- Copyright (c) YugaByte, Inc.

-- Encrypt the details column in the provider tables
ALTER TABLE telemetry_provider 
ALTER COLUMN config 
TYPE bytea USING pgp_sym_encrypt(config::text, 'telemetry_provider::config');
