import { ArchitectureType } from '../../../../../components/configRedesign/providerRedesign/constants';
import { ProviderMin } from '../form/fields/ProvidersField/ProvidersField';
import { AuditLogConfig } from '../../universe-tabs/db-audit-logs/utils/types';

//This File has enum, interfaces, dto related Universe Form and divided to help in finding theme easily
//--------------------------------------------------------- Most Used OR Common Types - Starts --------------------------------------------------------
export enum ClusterModes {
  CREATE = 'CREATE',
  EDIT = 'EDIT'
}

export enum ClusterType {
  PRIMARY = 'PRIMARY',
  ASYNC = 'ASYNC'
}

export enum UpdateActions {
  FULL_MOVE = 'FULL_MOVE',
  SMART_RESIZE = 'SMART_RESIZE',
  SMART_RESIZE_NON_RESTART = 'SMART_RESIZE_NON_RESTART',
  UPDATE = 'UPDATE'
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

export enum MasterPlacementMode {
  COLOCATED = 'COLOCATED',
  DEDICATED = 'DEDICATED'
}

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

export enum ExposingServiceTypes {
  EXPOSED = 'EXPOSED',
  UNEXPOSED = 'UNEXPOSED'
}

//-------------------------------------------------------- Most Used OR Common Types - Ends --------------------------------------------------------

//-------------------------------------------------------- Payload related Types - Starts ----------------------------------------------------------

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
  defaultRegion?: string | null;
}

export interface UserIntent {
  universeName: string;
  provider: string;
  providerType: CloudType;
  replicationFactor: number;
  regionList: string[];
  instanceType: string | null;
  masterInstanceType?: string | null;
  tserverK8SNodeResourceSpec?: K8NodeSpec | null;
  masterK8SNodeResourceSpec?: K8NodeSpec | null;
  numNodes: number;
  masterNumNodes?: number;
  ybSoftwareVersion: string | null;
  deviceInfo: DeviceInfo | null;
  masterDeviceInfo?: DeviceInfo | null;
  enableYSQL: boolean;
  enableYSQLAuth: boolean;
  enableYCQL: boolean;
  enableYCQLAuth: boolean;
  enableExposingService: ExposingServiceTypes | null;
  useSystemd: boolean;
  //optional fields
  accessKeyCode?: string | null;
  dedicatedNodes?: boolean;
  assignPublicIP?: boolean;
  useTimeSync?: boolean;
  ysqlPassword?: string | null;
  ycqlPassword?: string | null;
  enableNodeToNodeEncrypt?: boolean;
  enableClientToNodeEncrypt?: boolean;
  awsArnString?: string | null;
  enableYEDIS?: boolean;
  enableIPV6?: boolean;
  ybcPackagePath?: string | null;
  instanceTags?: Record<string, string>;
  specificGFlags?: {
    inheritFromPrimary: boolean;
    perProcessFlags: {};
    perAZ?: {};
  };
  masterGFlags?: Record<string, any>;
  tserverGFlags?: Record<string, any>;
  universeOverrides?: string;
  userIntentOverrides?: {
    azOverrides?: Record<string, string>;
  };
  proxyConfig?: {};
  useSpotInstance?: boolean | null;
  imageBundleUUID?: string;
  auditLogConfig?: AuditLogConfig;
}

export interface Cluster {
  placementInfo?: {
    cloudList: PlacementCloud[];
  };
  clusterType: ClusterType;
  userIntent: UserIntent;
  regions?: any;
  uuid?: string;
}

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

export interface NodeDetails {
  nodeIdx: number;
  nodeName: string | null;
  nodeUuid: string | null;
  placementUuid: string;
  state: NodeState;
  cloudInfo?: CloudInfo;
}

export interface EncryptionAtRestConfig {
  encryptionAtRestEnabled?: boolean;
  configUUID?: string;
  kmsConfigUUID?: string; // KMS config Id field for configure/create calls
  key_op?: 'ENABLE' | 'DISABLE' | 'UNDEFINED'; // operation field for configure/create calls
  type?: 'DATA_KEY' | 'CMK';
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
  universeUUID: string;
  updateInProgress: boolean;
  updateSucceeded: boolean;
  userAZSelected: boolean;
  enableYbc: boolean;
  updateOptions: string[];
  useSpotInstance: boolean;
  arch: ArchitectureType;
  softwareUpgradeState: string;
  prevYBSoftwareConfig: { softwareVersion: string };
  universePaused: boolean;
  xclusterInfo: any;
}

export type UniverseConfigure = Partial<UniverseDetails>;

export interface Resources {
  azList: string[];
  ebsPricePerHour: number;
  memSizeGB: number;
  numCores: number;
  numNodes: number;
  masterNumNodes?: number;
  pricePerHour: number;
  volumeCount: number;
  volumeSizeGB: number;
}

export interface UniverseConfig {
  disableAlertsUntilSecs: string;
  takeBackups: string;
}
export interface Universe {
  creationDate: string;
  name: string;
  resources: Resources;
  universeConfig: UniverseConfig;
  universeDetails: UniverseDetails;
  universeUUID: string;
  version: number;
}

//-------------------------------------------------------- Payload related Types - Ends -------------------------------------------------------------------

//-------------------------------------------------------- Form Data related Types - Starts ---------------------------------------------------------------

export interface Placement {
  uuid: string;
  name: string;
  replicationFactor: number;
  subnet: string;
  numNodesInAZ: number;
  isAffinitized: boolean;
  parentRegionId: string;
  parentRegionName: string;
  parentRegionCode: string;
}

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

export interface DeviceInfo {
  volumeSize: number;
  numVolumes: number;
  diskIops: number | null;
  throughput: number | null;
  storageClass: 'standard'; // hardcoded in DeviceInfo.java
  mountPoints: string | null;
  storageType: StorageType | null;
}

export interface K8NodeSpec {
  memoryGib: number;
  cpuCoreCount: number;
}
//-------------------------------------------------------- Most Used OR Common Types - Ends --------------------------------------------------------

//-------------------------------------------------------- Payload related Types - Starts ----------------------------------------------------------

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
  defaultRegion?: string | null;
}

export interface UserIntent {
  universeName: string;
  provider: string;
  providerType: CloudType;
  replicationFactor: number;
  regionList: string[];
  instanceType: string | null;
  numNodes: number;
  ybSoftwareVersion: string | null;
  deviceInfo: DeviceInfo | null;
  enableYSQL: boolean;
  enableYSQLAuth: boolean;
  enableYCQL: boolean;
  enableYCQLAuth: boolean;
  enableExposingService: ExposingServiceTypes | null;
  useSystemd: boolean;
  //optional fields
  accessKeyCode?: string | null;
  dedicatedNodes?: boolean;
  assignPublicIP?: boolean;
  useTimeSync?: boolean;
  ysqlPassword?: string | null;
  ycqlPassword?: string | null;
  enableNodeToNodeEncrypt?: boolean;
  enableClientToNodeEncrypt?: boolean;
  awsArnString?: string | null;
  enableYEDIS?: boolean;
  enableIPV6?: boolean;
  ybcPackagePath?: string | null;
  instanceTags?: Record<string, string>;
  masterGFlags?: Record<string, any>;
  tserverGFlags?: Record<string, any>;
  universeOverrides?: string;
  azOverrides?: Record<string, string>;
}

export interface Cluster {
  placementInfo?: {
    cloudList: PlacementCloud[];
  };
  clusterType: ClusterType;
  userIntent: UserIntent;
  regions?: any;
}

export interface CloudInfo {
  assignPublicIP: boolean;
  private_ip?: string;
  az?: string;
  region?: string;
}

export interface NodeDetails {
  nodeIdx: number;
  nodeName: string | null;
  nodeUuid: string | null;
  placementUuid: string;
  state: NodeState;
  cloudInfo?: CloudInfo;
}

export interface EncryptionAtRestConfig {
  encryptionAtRestEnabled?: boolean;
  configUUID?: string;
  kmsConfigUUID?: string; // KMS config Id field for configure/create calls
  key_op?: 'ENABLE' | 'DISABLE' | 'UNDEFINED'; // operation field for configure/create calls
  type?: 'DATA_KEY' | 'CMK';
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
  encryptionAtRestConfig: EncryptionAtRestConfig;
  errorString: string | null;
  expectedUniverseVersion: number;
  importedState: 'NONE' | 'STARTED' | 'MASTERS_ADDED' | 'TSERVERS_ADDED' | 'IMPORTED';
  itestS3PackagePath: string;
  nextClusterIndex: number;
  nodeDetailsSet: NodeDetails[];
  nodePrefix: string;
  resetAZConfig: boolean;
  rootCA: string;
  universeUUID: string;
  updateInProgress: boolean;
  updateSucceeded: boolean;
  userAZSelected: boolean;
  enableYbc: boolean;
  updateOptions: string[];
  xclusterInfo: any;
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
export interface Universe {
  creationDate: string;
  name: string;
  resources: Resources;
  universeConfig: UniverseConfig;
  universeDetails: UniverseDetails;
  universeUUID: string;
  version: number;
}

//-------------------------------------------------------- Payload related Types - Ends -------------------------------------------------------------------

//-------------------------------------------------------- Form Data related Types - Starts ---------------------------------------------------------------

export interface Placement {
  uuid: string;
  name: string;
  replicationFactor: number;
  subnet: string;
  numNodesInAZ: number;
  isAffinitized: boolean;
  parentRegionId: string;
  parentRegionName: string;
  parentRegionCode: string;
}

export interface CloudConfigFormValue {
  universeName: string;
  provider: ProviderMin | null;
  regionList: string[]; // array of region IDs
  numNodes: number;
  masterNumNodes?: number;
  replicationFactor: number;
  autoPlacement?: boolean;
  placements: Placement[];
  defaultRegion?: string | null;
  resetAZConfig?: boolean;
  userAZSelected?: boolean;
  mastersInDefaultRegion?: boolean;
  masterPlacement?: MasterPlacementMode;
}

export interface InstanceConfigFormValue {
  instanceType: string | null;
  useSpotInstance?: boolean | null;
  masterInstanceType?: string | null;
  deviceInfo: DeviceInfo | null;
  masterDeviceInfo?: DeviceInfo | null;
  tserverK8SNodeResourceSpec?: K8NodeSpec | null;
  masterK8SNodeResourceSpec?: K8NodeSpec | null;
  assignPublicIP: boolean;
  useTimeSync: boolean;
  enableClientToNodeEncrypt: boolean;
  enableNodeToNodeEncrypt: boolean;
  rootCA: string;
  enableEncryptionAtRest: boolean;
  enableYSQL: boolean;
  enableYSQLAuth: boolean;
  ysqlPassword?: string;
  ysqlConfirmPassword?: string;
  enableYCQL: boolean;
  enableYCQLAuth: boolean;
  ycqlPassword?: string;
  ycqlConfirmPassword?: string;
  enableYEDIS: boolean;
  kmsConfig: string | null;
  arch?: ArchitectureType | null;
  imageBundleUUID?: string | null;
}

export interface AdvancedConfigFormValue {
  useSystemd: boolean;
  ybcPackagePath: string | null;
  awsArnString: string | null;
  enableIPV6: boolean;
  enableExposingService: ExposingServiceTypes | null;
  customizePort: boolean;
  accessKeyCode: string | null;
  ybSoftwareVersion: string | null;
  communicationPorts: CommunicationPorts;
}

export interface InstanceTag {
  name: string;
  value: string;
  id?: string;
}
export type InstanceTags = InstanceTag[];

export interface Gflag {
  Name: string;
  MASTER?: string | boolean | number;
  TSERVER?: string | boolean | number;
  tags?: string;
}

export interface UniverseFormData {
  cloudConfig: CloudConfigFormValue;
  instanceConfig: InstanceConfigFormValue;
  advancedConfig: AdvancedConfigFormValue;
  instanceTags: InstanceTags;
  gFlags: Gflag[];
  inheritFlagsFromPrimary?: boolean;
  universeOverrides?: string;
  azOverrides?: Record<string, string>;
  proxyConfig?: {};
  specificGFlagsAzOverrides?: {};
}

//Default data
export const DEFAULT_COMMUNICATION_PORTS: CommunicationPorts = {
  masterHttpPort: 7000,
  masterRpcPort: 7100,
  tserverHttpPort: 9000,
  tserverRpcPort: 9100,
  redisServerHttpPort: 11000,
  redisServerRpcPort: 6379,
  yqlServerHttpPort: 12000,
  yqlServerRpcPort: 9042,
  ysqlServerHttpPort: 13000,
  ysqlServerRpcPort: 5433,
  nodeExporterPort: 9300
};

export const DEFAULT_CLOUD_CONFIG: CloudConfigFormValue = {
  universeName: '',
  provider: null,
  regionList: [],
  numNodes: 3,
  masterNumNodes: 3,
  replicationFactor: 3,
  autoPlacement: true, // "AUTO" is the default value when creating new universe
  placements: [],
  defaultRegion: null,
  mastersInDefaultRegion: false,
  masterPlacement: MasterPlacementMode.COLOCATED,
  resetAZConfig: false,
  userAZSelected: false
};

export const DEFAULT_INSTANCE_CONFIG: InstanceConfigFormValue = {
  instanceType: null,
  masterInstanceType: null,
  useSpotInstance: null,
  deviceInfo: null,
  masterDeviceInfo: null,
  tserverK8SNodeResourceSpec: null,
  masterK8SNodeResourceSpec: null,
  assignPublicIP: true,
  useTimeSync: true,
  enableClientToNodeEncrypt: true,
  enableNodeToNodeEncrypt: true,
  rootCA: '',
  enableEncryptionAtRest: false,
  enableYSQL: true,
  enableYSQLAuth: true,
  ysqlPassword: '',
  ysqlConfirmPassword: '',
  enableYCQL: true,
  enableYCQLAuth: true,
  ycqlPassword: '',
  ycqlConfirmPassword: '',
  enableYEDIS: false,
  kmsConfig: null,
  arch: null,
  imageBundleUUID: ''
};

export const DEFAULT_ADVANCED_CONFIG: AdvancedConfigFormValue = {
  useSystemd: true,
  ybcPackagePath: null,
  awsArnString: '',
  enableIPV6: false,
  enableExposingService: null,
  customizePort: false,
  accessKeyCode: '',
  ybSoftwareVersion: null,
  communicationPorts: DEFAULT_COMMUNICATION_PORTS
};

export const DEFAULT_USER_TAGS = [{ name: '', value: '' }];
export const DEFAULT_GFLAGS = [];
export const DEFAULT_UNIVERSE_OVERRIDES = '';
export const DEFAULT_AZ_OVERRIDES = {};

export const DEFAULT_FORM_DATA: UniverseFormData = {
  cloudConfig: DEFAULT_CLOUD_CONFIG,
  instanceConfig: DEFAULT_INSTANCE_CONFIG,
  advancedConfig: DEFAULT_ADVANCED_CONFIG,
  instanceTags: DEFAULT_USER_TAGS,
  gFlags: DEFAULT_GFLAGS,
  inheritFlagsFromPrimary: true,
  universeOverrides: DEFAULT_UNIVERSE_OVERRIDES,
  azOverrides: DEFAULT_AZ_OVERRIDES
};
//-------------------------------------------------------- Form Data related Types - Ends -------------------------------------------------------------------

//-------------------------------------------------------- Remaining types - Field/API level - Starts -------------------------------------------------------

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
export interface AvailabilityZone {
  uuid: string;
  code: string;
  name: string;
  active: boolean;
  subnet: string;
}

export interface AZOverrides {
  value: string;
  id?: string;
}
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

export interface YBSoftwareMetadata {
  state: string;
  notes: string[];
  filePath: string[];
  chartPath: string;
  imageTag: string;
  packages: YBSoftwareMetadataPackages[];
}

export interface YBSoftwareMetadataPackages {
  path: string;
  arch: string;
}

export interface Provider {
  uuid: string;
  code: CloudType;
  name: string;
  active: boolean;
  customerUUID: string;
  details: Record<string, any>;
}
export interface RegionInfo {
  parentRegionId: string;
  parentRegionName: string;
  parentRegionCode: string;
}

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

export enum VolumeType {
  EBS = 'EBS',
  SSD = 'SSD',
  HDD = 'HDD',
  NVME = 'NVME'
}
interface VolumeDetails {
  volumeSizeGB: number;
  volumeType: VolumeType;
  mountPath: string;
}

interface InstanceTypeDetails {
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

export interface InstanceTypeWithGroup extends InstanceType {
  groupName: string;
}

export interface RunTimeConfigEntry {
  inherited: boolean;
  key: string;
  value: string;
}
export interface RunTimeConfig {
  configEntries: RunTimeConfigEntry[];
  mutableScope: boolean;
  uuid: string;
  type: 'GLOBAL';
}

export interface OverridesError {
  errorString: string;
}
export interface HelmOverridesError {
  overridesErrors: OverridesError[];
}

export interface UniverseResource {
  azList: string[];
  ebsPricePerHour: number;
  gp3FreePiops: number;
  gp3FreeThroughput: number;
  memSizeGB: number;
  numCores: number;
  numNodes: number;
  masterNumNodes?: number;
  pricePerHour: number;
  pricingKnown: boolean;
  volumeCount: number;
  volumeSizeGB: number;
}

export interface UniverseFormConfigurationProps {
  runtimeConfigs: any;
}

export enum ImageBundleType {
  YBA_ACTIVE = 'YBA_ACTIVE',
  YBA_DEPRECATED = 'YBA_DEPRECATED',
  CUSTOM = 'CUSTOM'
}
export interface ImageBundle {
  uuid: string;
  name: string;
  details: {
    arch: ArchitectureType;
    globalYbImage: string;
    regions: {
      [key: string]: {
        ybImage: string;
      };
    };
    sshUser: string;
    sshPort: number;
    useIMDSv2?: boolean;
  };
  useAsDefault: boolean;
  metadata?: {
    type: ImageBundleType;
    version: string;
  };
  active: true;
}

//-------------------------------------------------------- Remaining types - Field/API Ends -------------------------------------------------------------------
