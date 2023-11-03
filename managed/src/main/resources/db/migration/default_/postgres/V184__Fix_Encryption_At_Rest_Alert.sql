-- Copyright (c) YugaByte, Inc.

-- ENCRYPTION_AT_REST_CONFIG_EXPIRY
select replace_configuration_query(
 'ENCRYPTION_AT_REST_CONFIG_EXPIRY',
 'ybp_universe_encryption_key_expiry_day{universe_uuid="__universeUuid__"} '
   || '{{ query_condition }} {{ query_threshold }}');
