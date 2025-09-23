-- Copyright (c) YugaByte, Inc.

-- Recreate alert definition with new affected_node_names
update alert_definition set config_written = false where configuration_uuid IN
  (select uuid from alert_configuration where template in
    ('CLIENT_TO_NODE_CA_CERT_EXPIRY',
     'CLIENT_TO_NODE_CERT_EXPIRY',
     'NODE_TO_NODE_CA_CERT_EXPIRY',
     'NODE_TO_NODE_CERT_EXPIRY')
  );
