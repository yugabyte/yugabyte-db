-- Copyright (c) YugaByte, Inc.

ALTER TABLE alert_channel ALTER COLUMN params TYPE bytea USING pgp_sym_encrypt(params::text, 'alert_channel::params');
