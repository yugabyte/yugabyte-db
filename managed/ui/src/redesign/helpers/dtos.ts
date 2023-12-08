import {
  NodeAggregation,
  SplitMode,
  SplitType,
  TimeAggregation
} from '../../components/metrics/dtos';
import { MetricName } from '../../components/xcluster/constants';
import { YBTableRelationType } from './constants';
import { DeepPartial } from './types';

export interface HostInfo {
  aws:
    | {
        'instance-id': string;
        privateIp: string;
        region: string;
        'vpc-id': string;
      }
    | string;
  gcp:
    | {
        network: string;
        project: string;
      }
    | string;
}

export interface SuggestedKubernetesConfig {
  config: {
    KUBECONFIG_PULL_SECRET_NAME: string;
    KUBECONFIG_PULL_SECRET_CONTENT: any;
    KUBECONFIG_IMAGE_REGISTRY: string;
    KUBECONFIG_PROVIDER: string;
  };
  regionList: {
    code: string;
    name: string;
    latitude: number;
    longitude: number;
    zoneList: { config: { STORAGE_CLASS: string }; name: string }[];
  }[];
  name: string;
}

export interface PlacementAZ {
  uuid: string;
  name: string;
  replicationFactor: number;
  subnet: string;
  numNodesInAZ: number;
  isAffinitized: boolean;
}

export interface PlacementRegion {
  uuid: string;
  code: string;
  name: string;
  azList: PlacementAZ[];
}

export interface PlacementCloud {
  uuid: string;
  code: string;
  regionList: PlacementRegion[];
}

export enum CloudType {
  unknown = 'unknown',
  aws = 'aws',
  gcp = 'gcp',
  azu = 'azu',
  docker = 'docker',
  onprem = 'onprem',
  kubernetes = 'kubernetes',
  cloud = 'cloud-1',
  other = 'other'
}

// PublicCloudConstants.java
export enum StorageType {
  IO1 = 'IO1',
  GP2 = 'GP2',
  GP3 = 'GP3',
  Scratch = 'Scratch',
  Persistent = 'Persistent',
  StandardSSD_LRS = 'StandardSSD_LRS',
  Premium_LRS = 'Premium_LRS',
  UltraSSD_LRS = 'UltraSSD_LRS'
}

export interface DeviceInfo {
  volumeSize: number;
  numVolumes: number;
  diskIops: number | null;
  throughput: number | null;
  storageClass: 'standard'; // hardcoded in DeviceInfo.java
  mountPoints: string | null;
  storageType: StorageType | null;
}

// UniverseTaskParams.java
export interface CommunicationPorts {
  masterHttpPort: number;
  masterRpcPort: number;
  tserverHttpPort: number;
  tserverRpcPort: number;
  redisServerHttpPort: number;
  redisServerRpcPort: number;
  yqlServerHttpPort: number;
  yqlServerRpcPort: number;
  ysqlServerHttpPort: number;
  ysqlServerRpcPort: number;
  nodeExporterPort: number;
}

export type FlagsArray = { name: string; value: string }[];
export type FlagsObject = Record<string, string>;

// UniverseDefinitionTaskParams.java
export interface UserIntent {
  universeName: string;
  provider: string;
  providerType: CloudType;
  replicationFactor: number;
  regionList: string[];
  preferredRegion: string | null;
  instanceType: string;
  numNodes: number;
  ybSoftwareVersion: string | null;
  accessKeyCode: string | null;
  deviceInfo: DeviceInfo | null;
  assignPublicIP: boolean;
  useTimeSync: boolean;
  enableYSQL: boolean;
  enableYSQLAuth: boolean;
  enableYCQL: boolean;
  enableYCQLAuth: boolean;
  enableNodeToNodeEncrypt: boolean;
  enableClientToNodeEncrypt: boolean;
  enableVolumeEncryption: boolean;
  awsArnString: string;
  useHostname: boolean;
  // api returns tags as FlagsObject but when creating/editing the universe - it expects tags as FlagsArray
  masterGFlags: FlagsObject | FlagsArray;
  tserverGFlags: FlagsObject | FlagsArray;
  instanceTags: FlagsObject | FlagsArray;
}

export const ClusterType = {
  PRIMARY: 'PRIMARY',
  ASYNC: 'ASYNC'
} as const;
export type ClusterType = typeof ClusterType[keyof typeof ClusterType];

export interface Cluster {
  placementInfo: {
    cloudList: PlacementCloud[];
  };
  clusterType: ClusterType;
  userIntent: UserIntent;
  uuid: string;
  index: number;
}

export interface Resources {
  azList: string[];
  ebsPricePerHour: number;
  memSizeGB: number;
  numCores: number;
  numNodes: number;
  pricePerHour: number;
  volumeCount: number;
  volumeSizeGB: number;
}

export interface UniverseConfig {
  disableAlertsUntilSecs: string;
  takeBackups: string;
}

export interface EncryptionAtRestConfig {
  readonly encryptionAtRestEnabled: boolean;
  readonly kmsConfigUUID: string | null; // KMS config Id in universe json
  readonly opType: 'ENABLE' | 'DISABLE' | 'UNDEFINED';
  configUUID?: string; // KMS config Id field for configure/create calls
  key_op?: 'ENABLE' | 'DISABLE' | 'UNDEFINED'; // operation field for configure/create calls
  type?: 'DATA_KEY' | 'CMK';
}

// NodeDetails.java
export enum NodeState {
  ToBeAdded = 'ToBeAdded',
  Provisioned = 'Provisioned',
  SoftwareInstalled = 'SoftwareInstalled',
  UpgradeSoftware = 'UpgradeSoftware',
  UpdateGFlags = 'UpdateGFlags',
  Live = 'Live',
  Stopping = 'Stopping',
  Starting = 'Starting',
  Stopped = 'Stopped',
  Unreachable = 'Unreachable',
  ToBeRemoved = 'ToBeRemoved',
  Removing = 'Removing',
  Removed = 'Removed',
  Adding = 'Adding',
  BeingDecommissioned = 'BeingDecommissioned',
  Decommissioned = 'Decommissioned'
}

// NodeDetails.java
export interface NodeDetails {
  nodeIdx: number;
  nodeName: string | null;
  nodeUuid: string | null;
  placementUuid: string;
  state: NodeState;
}

export interface UniverseDetails {
  currentClusterType?: ClusterType; // used in universe configure calls
  clusterOperation?: 'CREATE' | 'EDIT' | 'DELETE';
  allowInsecure: boolean;
  backupInProgress: boolean;
  mastersInDefaultRegion?: boolean;
  capability: 'READ_ONLY' | 'EDITS_ALLOWED';
  clusters: Cluster[];
  communicationPorts: CommunicationPorts;
  cmkArn: string;
  deviceInfo: DeviceInfo | null;
  masterDeviceInfo: DeviceInfo | null;
  encryptionAtRestConfig: EncryptionAtRestConfig;
  extraDependencies: {
    installNodeExporter: boolean;
  };
  errorString: string | null;
  expectedUniverseVersion: number;
  importedState: 'NONE' | 'STARTED' | 'MASTERS_ADDED' | 'TSERVERS_ADDED' | 'IMPORTED';
  itestS3PackagePath: string;
  nextClusterIndex: number;
  nodeDetailsSet: NodeDetails[];
  nodePrefix: string;
  resetAZConfig: boolean;
  rootCA: string;
  clientRootCA: string;
  rootAndClientRootCASame: boolean;
  xclusterInfo: {
    sourceRootCertDirPath: string;
    sourceXClusterConfigs: string[];
    targetXClusterConfigs: string[];
  };
  universeUUID: string;
  updateInProgress: boolean;
  updateSucceeded: boolean;
  userAZSelected: boolean;
  enableYbc: boolean;
  updateOptions: string[];
  useSpotInstance: boolean;
}

export type UniverseConfigure = DeepPartial<UniverseDetails>;

export interface Universe {
  creationDate: string;
  drConfigUuidsAsSource: string[];
  drConfigUuidsAsTarget: string[];
  name: string;
  resources: Resources;
  universeConfig: UniverseConfig;
  universeDetails: UniverseDetails;
  universeUUID: string;
  version: number;
}

export const TableType = {
  YQL_TABLE_TYPE: 'YQL_TABLE_TYPE',
  REDIS_TABLE_TYPE: 'REDIS_TABLE_TYPE',
  PGSQL_TABLE_TYPE: 'PGSQL_TABLE_TYPE',
  TRANSACTION_STATUS_TABLE_TYPE: 'TRANSACTION_STATUS_TABLE_TYPE'
} as const;
export type TableType = typeof TableType[keyof typeof TableType];

export interface YBTable {
  isIndexTable: boolean;
  keySpace: string;
  pgSchemaName: string;
  relationType: YBTableRelationType;
  sizeBytes: number;
  tableName: string;
  tableType: TableType;
  tableUUID: string;
}

export interface UniverseNamespace {
  namespaceUUID: string;
  name: string;
  tableType: TableType;
}

// Provider.java
export interface Provider {
  uuid: string;
  code: CloudType;
  name: string;
  active: boolean;
  customerUUID: string;
}

export interface AvailabilityZone {
  uuid: string;
  code: string;
  name: string;
  active: boolean;
  subnet: string;
}

// Region.java
export interface Region {
  uuid: string;
  code: string;
  name: string;
  ybImage: string;
  longitude: number;
  latitude: number;
  active: boolean;
  securityGroupId: string | null;
  details: string | null;
  zones: AvailabilityZone[];
}

// InstanceType.java
export interface VolumeDetails {
  volumeSizeGB: number;
  volumeType: 'EBS' | 'SSD' | 'HDD' | 'NVME';
  mountPath: string;
}

export interface InstanceTypeDetails {
  tenancy: 'Shared' | 'Dedicated' | 'Host' | null;
  volumeDetailsList: VolumeDetails[];
}
export interface InstanceType {
  active: boolean;
  providerCode: CloudType;
  instanceTypeCode: string;
  idKey: {
    providerCode: CloudType;
    instanceTypeCode: string;
  };
  numCores: number;
  memSizeGB: number;
  instanceTypeDetails: InstanceTypeDetails;
}

// AccessKey.java
export interface AccessKey {
  idKey: {
    keyCode: string;
    providerUUID: string;
  };
  keyInfo: {
    publicKey: string;
    privateKey: string;
    vaultPasswordFile: string;
    vaultFile: string;
    sshUser: string;
    sshPort: number;
    airGapInstall: boolean;
    passwordlessSudoAccess: boolean;
    provisionInstanceScript: string;
  };
}

// CertificateInfo.java
export interface Certificate {
  uuid: string;
  customerUUID: string;
  label: string;
  startDate: string;
  expiryDate: string;
  privateKey: string;
  certificate: string;
  certType: 'SelfSigned' | 'CustomCertHostPath';
}

// EncryptionAtRestController.java
export interface KmsConfig {
  credentials: {
    AWS_ACCESS_KEY_ID: string;
    AWS_REGION: string;
    AWS_SECRET_ACCESS_KEY: string;
    cmk_id: string;
  };
  metadata: {
    configUUID: string;
    in_use: boolean;
    name: string;
    provider: string;
  };
}

export interface PitrConfig {
  createTime: string;
  customerUUID: string;
  dbName: string;
  maxRecoverTimeInMillis: number;
  minRecoverTimeInMillis: number;
  retentionPeriod: number;
  scheduleInterval: number;
  tableType: TableType;
  updateTime: string;
  uuid: string;
}

export interface HAPlatformInstance {
  uuid: string;
  config_uuid: string;
  address: string;
  is_leader: boolean;
  is_local: boolean;
  last_backup: string | null;
}

export interface HAConfig {
  uuid: string;
  cluster_key: string;
  last_failover: number;
  instances: HAPlatformInstance[];
}

export interface HAReplicationSchedule {
  frequency_milliseconds: number;
  is_running: boolean;
}

export const TableTypeLabel: Record<TableType, string> = {
  YQL_TABLE_TYPE: 'YCQL',
  PGSQL_TABLE_TYPE: 'YSQL',
  REDIS_TABLE_TYPE: 'REDIS',
  TRANSACTION_STATUS_TABLE_TYPE: 'SYSTEM'
} as const;

export interface GraphFilter {
  startMoment: any;
  endMoment: any;
  nodeName: string;
  nodePrefix: string;
  filterValue?: string;
  filterType?: string;
  currentSelectedRegion?: string;
  metricMeasure?: string;
  outlierType?: string;
  outlierNumNodes?: number;
  selectedRegionClusterUUID?: string | null;
  selectedRegionCode?: string | null;
  selectedZoneName?: string | null;
  currentSelectedNodeType: string | null;
}

/**
 * Source: managed/src/main/java/com/yugabyte/yw/metrics/MetricSettings.java
 */
export interface MetricSettings {
  metric: string;

  nodeAggregation?: NodeAggregation;
  timeAggregation?: TimeAggregation;
  splitMode?: SplitMode;
  splitType?: SplitType;
  // splitCount is ignored when constructing the metric query if splitMode is
  // SplitMode.NONE.
  splitCount?: number;
  returnAggregatedValue?: boolean;
}

/**
 * Source: managed/src/main/java/com/yugabyte/yw/forms/MetricQueryParams.java
 */
export interface MetricsQueryParams {
  start: string;

  availabilityZones?: string[];
  clusterUuids?: string[];
  end?: string;
  isRecharts?: boolean;
  // `metrics` gets added to `metricsSettingsMap` on the backend
  // with default settings.
  // See MetricsQueryHelper.java for details.
  metrics?: MetricName[];
  metricsWithSettings?: MetricSettings[];
  namespaceId?: string;
  nodeNames?: string[];
  nodePrefix?: string;
  regionCode?: string[];
  serverType?: string;
  streamId?: string;
  tableId?: string;
  tableName?: string;
  xClusterConfigUuid?: string;
}

// ---------------------------------------------------------------------------
// Metric Respose Types
// Source:
// - Response JSON from MetricsQueryExecutor.java
export interface MetricTrace {
  name: string;
  type: string;
  x: number[];
  y: string[] | number[];

  instanceName?: string;
  tableId?: string;
  tableName?: string;
  namespaceId?: string;
  namespaceName?: string;
  mode?: string;
  line?: {
    dash: string;
    width: number;
  };
}

export interface Metric {
  data: MetricTrace[];
  directURLs: string[];
  layout: {
    title: string;
    xaxis: {
      alias: { [x: string]: string };
      type: string;
    };
    yaxis: {
      alias: { [x: string]: string };
      ticksuffix: string;
    };
  };
  metricsLinkUseBrowserFqdn: boolean;
  queryKey: string;
}

export type MetricsQueryResponse = {
  [metricName in MetricName]?: Metric;
};
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Platform Result Types
// Sources:
// src/main/java/com/yugabyte/yw/forms/PlatformResults.java
// src/main/java/com/yugabyte/yw/models/helpers/BaseBeanValidator.java
// ---------------------------------------------------------------------------
export interface YBPTask {
  taskUUID: string;

  resourceUUID?: string;
}

export interface YBPSuccess {
  message: string;
  success: true;
}

export type YBPError = {
  error: string;
  httpMethod: string;
  requestUri: string;
  success: false;

  errorJson?: {};
};

// TODO: Remove when YBPStructuredError is replaced by YBPError(json)
export interface YBPStructuredError {
  error: {};
  success: false;
}

export interface YBPBeanValidationError extends YBPStructuredError {
  error: {
    errorSource: string[];
    [x: string]: string[];
  };
  success: false;
}

// ---------------------------------------------------------------------------
