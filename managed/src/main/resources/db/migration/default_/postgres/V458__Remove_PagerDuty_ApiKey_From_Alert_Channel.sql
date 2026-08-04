-- Copyright (c) YugabyteDB, Inc.

-- AlertChannelPagerDutyParams no longer declares `apiKey` (see PLAT-21772): only routingKey is
-- used when signing PagerDuty Events-v2 requests, so any stored value is dead credential
-- material. Ebean deserialisation is already tolerant thanks to @JsonIgnoreProperties on the
-- params class, but we still strip the value from persisted rows so it does not linger as
-- encrypted-at-rest sensitive data.

-- The alert_channel.params column is a pgp_sym_encrypt bytea blob using the standard
-- 'alert_channel::params' key (see V243__Alter_Alert_Channel_Params.sql). Decrypt, drop the key
-- from the JSON payload with the jsonb '-' operator, and re-encrypt in place. Only touching
-- PagerDuty rows keeps the write set small and avoids re-encrypting unrelated channels.

UPDATE alert_channel
SET params = pgp_sym_encrypt(
    ((pgp_sym_decrypt(params, 'alert_channel::params')::jsonb) - 'apiKey')::text,
    'alert_channel::params')
WHERE (pgp_sym_decrypt(params, 'alert_channel::params')::jsonb ->> 'channelType') = 'PagerDuty'
  AND (pgp_sym_decrypt(params, 'alert_channel::params')::jsonb) ? 'apiKey';
