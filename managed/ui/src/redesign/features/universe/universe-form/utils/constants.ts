// ------------Universe Form Fields Path Start------------

//Cloud config
export const UNIVERSE_NAME_FIELD = 'cloudConfig.universeName';
export const PROVIDER_FIELD = 'cloudConfig.provider';
export const REGIONS_FIELD = 'cloudConfig.regionList';
export const REPLICATION_FACTOR_FIELD = 'cloudConfig.replicationFactor';
export const AUTO_PLACEMENT_FIELD = 'cloudConfig.autoPlacement';
export const TOTAL_NODES_FIELD = 'cloudConfig.numNodes';
export const MASTER_TOTAL_NODES_FIELD = 'cloudConfig.masterNumNodes';
export const PLACEMENTS_FIELD = 'cloudConfig.placements';
export const DEFAULT_REGION_FIELD = 'cloudConfig.defaultRegion';
export const MASTERS_IN_DEFAULT_REGION_FIELD = 'cloudConfig.mastersInDefaultRegion';
export const MASTER_PLACEMENT_FIELD = 'cloudConfig.masterPlacement';
export const RESET_AZ_FIELD = 'cloudConfig.resetAZConfig';
export const USER_AZSELECTED_FIELD = 'cloudConfig.userAZSelected';

//Instance config
export const INSTANCE_TYPE_FIELD = 'instanceConfig.instanceType';
export const MASTER_INSTANCE_TYPE_FIELD = 'instanceConfig.masterInstanceType';
export const DEVICE_INFO_FIELD = 'instanceConfig.deviceInfo';
export const MASTER_DEVICE_INFO_FIELD = 'instanceConfig.masterDeviceInfo';
export const TSERVER_K8_NODE_SPEC_FIELD = 'instanceConfig.tserverK8SNodeResourceSpec';
export const MASTER_K8_NODE_SPEC_FIELD = 'instanceConfig.masterK8SNodeResourceSpec';
export const ASSIGN_PUBLIC_IP_FIELD = 'instanceConfig.assignPublicIP';
export const SPOT_INSTANCE_FIELD = 'instanceConfig.useSpotInstance';
export const YSQL_FIELD = 'instanceConfig.enableYSQL';
export const YSQL_AUTH_FIELD = 'instanceConfig.enableYSQLAuth';
export const YSQL_PASSWORD_FIELD = 'instanceConfig.ysqlPassword';
export const YSQL_CONFIRM_PASSWORD_FIELD = 'instanceConfig.ysqlConfirmPassword';
export const YCQL_FIELD = 'instanceConfig.enableYCQL';
export const YCQL_AUTH_FIELD = 'instanceConfig.enableYCQLAuth';
export const YCQL_PASSWORD_FIELD = 'instanceConfig.ycqlPassword';
export const YCQL_CONFIRM_PASSWORD_FIELD = 'instanceConfig.ycqlConfirmPassword';
export const YEDIS_FIELD = 'instanceConfig.enableYEDIS';
export const TIME_SYNC_FIELD = 'instanceConfig.useTimeSync';
export const CLIENT_TO_NODE_ENCRYPT_FIELD = 'instanceConfig.enableClientToNodeEncrypt';
export const ROOT_CERT_FIELD = 'instanceConfig.rootCA';
export const NODE_TO_NODE_ENCRYPT_FIELD = 'instanceConfig.enableNodeToNodeEncrypt';
export const EAR_FIELD = 'instanceConfig.enableEncryptionAtRest';
export const KMS_CONFIG_FIELD = 'instanceConfig.kmsConfig';
export const CPU_ARCHITECTURE_FIELD = 'instanceConfig.arch';
export const LINUX_VERSION_FIELD = 'instanceConfig.imageBundleUUID';

//Advanced config
export const SYSTEMD_FIELD = 'advancedConfig.useSystemd';
export const YBC_PACKAGE_PATH_FIELD = 'advancedConfig.ybcPackagePath';
export const AWS_ARN_STRING_FIELD = 'advancedConfig.awsArnString';
export const IPV6_FIELD = 'advancedConfig.enableIPV6';
export const EXPOSING_SERVICE_FIELD = 'advancedConfig.enableExposingService';
export const CUSTOMIZE_PORT_FIELD = 'advancedConfig.customizePort';
export const ACCESS_KEY_FIELD = 'advancedConfig.accessKeyCode';
export const SOFTWARE_VERSION_FIELD = 'advancedConfig.ybSoftwareVersion';
export const COMMUNICATION_PORTS_FIELD = 'advancedConfig.communicationPorts';
export const PG_COMPATIBILITY_FIELD = 'advancedConfig.enablePGCompatibitilty';

//Gflags
export const GFLAGS_FIELD = 'gFlags';
export const INHERIT_FLAGS_FROM_PRIMARY = 'inheritFlagsFromPrimary';

//Tags
export const USER_TAGS_FIELD = 'instanceTags';

//K8s overrides
export const UNIVERSE_OVERRIDES_FIELD = 'universeOverrides';
export const AZ_OVERRIDES_FIELD = 'azOverrides';

// ------------Universe Form Fields Path End------------

//Other form related constants
export const MIN_PLACEMENTS_FOR_GEO_REDUNDANCY = 3;

export const PRIMARY_FIELDS = [
  UNIVERSE_NAME_FIELD,
  PROVIDER_FIELD,
  REGIONS_FIELD,
  REPLICATION_FACTOR_FIELD,
  AUTO_PLACEMENT_FIELD,
  TOTAL_NODES_FIELD,
  MASTER_TOTAL_NODES_FIELD,
  PLACEMENTS_FIELD,
  DEFAULT_REGION_FIELD,
  MASTERS_IN_DEFAULT_REGION_FIELD,
  INSTANCE_TYPE_FIELD,
  MASTER_INSTANCE_TYPE_FIELD,
  GFLAGS_FIELD,
  USER_TAGS_FIELD,
  SOFTWARE_VERSION_FIELD,
  DEVICE_INFO_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  ASSIGN_PUBLIC_IP_FIELD,
  SYSTEMD_FIELD,
  TIME_SYNC_FIELD,
  YSQL_FIELD,
  YSQL_AUTH_FIELD,
  YSQL_PASSWORD_FIELD,
  YSQL_CONFIRM_PASSWORD_FIELD,
  YCQL_FIELD,
  YCQL_AUTH_FIELD,
  YCQL_PASSWORD_FIELD,
  YCQL_CONFIRM_PASSWORD_FIELD,
  IPV6_FIELD,
  EXPOSING_SERVICE_FIELD,
  YEDIS_FIELD,
  NODE_TO_NODE_ENCRYPT_FIELD,
  ROOT_CERT_FIELD,
  CLIENT_TO_NODE_ENCRYPT_FIELD,
  EAR_FIELD,
  KMS_CONFIG_FIELD,
  AWS_ARN_STRING_FIELD,
  COMMUNICATION_PORTS_FIELD,
  ACCESS_KEY_FIELD,
  CUSTOMIZE_PORT_FIELD,
  UNIVERSE_OVERRIDES_FIELD,
  AZ_OVERRIDES_FIELD,
  MASTER_PLACEMENT_FIELD,
  CPU_ARCHITECTURE_FIELD,
  LINUX_VERSION_FIELD
];

export const ASYNC_FIELDS = [
  UNIVERSE_NAME_FIELD,
  PROVIDER_FIELD,
  REGIONS_FIELD,
  REPLICATION_FACTOR_FIELD,
  AUTO_PLACEMENT_FIELD,
  TOTAL_NODES_FIELD,
  PLACEMENTS_FIELD,
  INSTANCE_TYPE_FIELD,
  SOFTWARE_VERSION_FIELD,
  DEVICE_INFO_FIELD,
  ASSIGN_PUBLIC_IP_FIELD,
  SYSTEMD_FIELD,
  TIME_SYNC_FIELD,
  YSQL_FIELD,
  YSQL_AUTH_FIELD,
  YCQL_FIELD,
  YCQL_AUTH_FIELD,
  IPV6_FIELD,
  EXPOSING_SERVICE_FIELD,
  YEDIS_FIELD,
  NODE_TO_NODE_ENCRYPT_FIELD,
  CLIENT_TO_NODE_ENCRYPT_FIELD,
  ACCESS_KEY_FIELD,
  ROOT_CERT_FIELD,
  EAR_FIELD,
  MASTER_PLACEMENT_FIELD,
  USER_TAGS_FIELD,
  INHERIT_FLAGS_FROM_PRIMARY,
  CPU_ARCHITECTURE_FIELD,
  LINUX_VERSION_FIELD
];

export const INHERITED_FIELDS_FROM_PRIMARY = [
  SOFTWARE_VERSION_FIELD,
  DEVICE_INFO_FIELD,
  ASSIGN_PUBLIC_IP_FIELD,
  SYSTEMD_FIELD,
  TIME_SYNC_FIELD,
  YSQL_FIELD,
  YSQL_AUTH_FIELD,
  YCQL_FIELD,
  YCQL_AUTH_FIELD,
  IPV6_FIELD,
  EXPOSING_SERVICE_FIELD,
  YEDIS_FIELD,
  NODE_TO_NODE_ENCRYPT_FIELD,
  CLIENT_TO_NODE_ENCRYPT_FIELD,
  ACCESS_KEY_FIELD,
  ROOT_CERT_FIELD,
  EAR_FIELD,
  USER_TAGS_FIELD,
  CPU_ARCHITECTURE_FIELD,
  LINUX_VERSION_FIELD
];

export const PASSWORD_REGEX = /^(?=.*[0-9])(?=.*[!@#$%^&*])(?=.*[a-z])(?=.*[A-Z])[a-zA-Z0-9!@#$%^&*]{8,256}$/;

export const TOAST_AUTO_DISMISS_INTERVAL = 3000;

export const DEFAULT_SLEEP_INTERVAL_IN_MS = 180000;
