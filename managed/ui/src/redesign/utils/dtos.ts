export interface RunTimeConfigData {
  configID: number;
  configKey: string;
  configValue: string;
  configTags: string[];
  isConfigInherited: boolean;
  displayName: string;
  helpTxt: string;
  type: string;
  scope: string;
}

export enum RunTimeConfigScope {
  GLOBAL = 'GLOBAL',
  UNIVERSE = 'UNIVERSE',
  PROVIDER = 'PROVIDER',
  CUSTOMER = 'CUSTOMER'
}

export interface RuntimeConfigScopeProps {
  configTagFilter: string[];
  fetchRuntimeConfigs: (scope?: string) => void;
  setRuntimeConfig: (key: string, value: string, scope?: string) => void;
  deleteRunTimeConfig: (key: string, scope?: string) => void;
  resetRuntimeConfigs: () => void;
}

export enum NodeType {
  Master = 'Master',
  TServer = 'TServer'
}

export enum InstanceRole {
  LEADER = 'LEADER'
}

export interface PerfRecommendationMetaData {
  suggestion?: string;
  maxNodeName: string;
  maxNodeValue: number;
  otherNodesAvgValue: number;
}

export interface HotShardData {
  suggestion?: string;
  hotShardNodeName: string;
  hotShardMaxNodeValue: number;
  hotShardAvgNodeValue: number;
}

export interface PerfRecommendationProps {
  data: PerfRecommendationMetaData;
  summary: React.ReactNode | string;
}

export interface HotShardRecommendation {
  data: HotShardData;
  summary: React.ReactNode | string;
}

export interface CustomRecommendation {
  summary: React.ReactNode | string;
  suggestion: string;
  type: string;
}

export interface IndexSchemaQueryData {
  table_name: string;
  index_name: string;
  index_command: string;
}

export interface IndexSchemaRecommendation {
  data: IndexSchemaQueryData[];
  summary: React.ReactNode | string;
}

export interface NodeDistributionData {
  numSelect: number;
  numInsert: number;
  numUpdate: number;
  numDelete: number;
}

export interface QueryLoadData {
  suggestion: string;
  maxNodeName: string;
  percentDiff: number;
  maxNodeDistribution: NodeDistributionData;
  otherNodesDistribution: NodeDistributionData;
}

export interface QueryLoadRecommendation {
  data: QueryLoadData;
  summary: React.ReactNode | string;
}

export enum RecommendationType {
  ALL = 'ALL',
  RANGE_SHARDING = 'RANGE_SHARDING',
  CPU_USAGE = 'CPU_USAGE',
  CONNECTION_SKEW = 'CONNECTION_SKEW',
  QUERY_LOAD_SKEW = 'QUERY_LOAD_SKEW',
  UNUSED_INDEX = 'UNUSED_INDEX',
  CPU_SKEW = 'CPU_SKEW',
  HOT_SHARD = 'HOT_SHARD',
  REJECTED_CONNECTIONS = 'REJECTED_CONNECTIONS'
}

export enum SortDirection {
  ASC = 'ASC',
  DESC = 'DESC'
}

const EntityType = {
  NODE: 'NODE',
  DATABASE: 'DATABASE',
  TABLE: 'TABLE',
  INDEX: 'INDEX',
  UNIVERSE: 'UNIVERSE'
} as const;
export type EntityType = typeof EntityType[keyof typeof EntityType];

const RecommendationPriority = {
  HIGH: 'HIGH',
  MEDIUM: 'MEDIUM',
  LOW: 'LOW'
} as const;
export type RecommendationPriority = typeof RecommendationPriority[keyof typeof RecommendationPriority];

const RecommendationState = {
  OPEN: 'OPEN',
  HIDDEN: 'HIDDEN',
  RESOLVED: 'RESOLVED'
} as const;
export type RecommendationState = typeof RecommendationState[keyof typeof RecommendationState];

interface HighestNodeQueryLoadDetails {
  DeleteStmt: number;
  InsertStmt: number;
  SelectStmt: number;
  UpdateStmt: number;
}

interface OtherNodeQueryLoadDetails extends HighestNodeQueryLoadDetails {}

export interface RecommendationInfo {
  // CPU Skew and CPU Usage
  timeInterval?: number;
  highestNodeCpu?: number;
  otherNodeCount?: number;
  highestNodeName?: string;
  otherNodesAvgCpu?: string;

  // Connection Skew
  node_with_highest_connection_count?: number;
  avg_connection_count_of_other_nodes?: number;
  details?: any;

  // Query Load Skew
  node_with_highest_query_load_details?: HighestNodeQueryLoadDetails;
  other_nodes_average_query_load_details?: OtherNodeQueryLoadDetails;

  // Hot Shard
  table_name_with_hot_shard?: string;
  database_name_with_hot_shard?: string;
  node_with_hot_shard?: string;
  avg_query_count_of_other_nodes?: number;
}

interface TableData {
  data: PerfRecommendationData[];
}

export interface PerfRecommendationData {
  type: RecommendationType;
  observation?: string;
  suggestion?: string;
  entityType?: EntityType;
  target: string;
  recommendationInfo?: RecommendationInfo;
  recommendationState?: RecommendationState;
  recommendationPriority?: RecommendationPriority;
  recommendationTimestamp?: number;
  isStale?: boolean;
  new?: boolean;
}

export interface IndexAndShardingRecommendationData {
  type: RecommendationType;
  target: string;
  indicator: number;
  table: TableData;
}

export interface LastRunData {
  customerUUID: string;
  endTime: string;
  manual: boolean;
  scheduleTime: string;
  startTime: string;
  state: string;
  universeUUID: string;
  uuid: string;
}

export interface NodeAgentEntities {
  archType: string;
  config: any;
  customerUuid: string;
  home: string;
  ip: string;
  name: string;
  osType: string;
  port: number;
  reachable: boolean;
  state: string;
  updatedAt: string;
  uuid: string;
  version: string;
  versionMatched: boolean;
}

export enum NodeAgentStatusList {
  READY = 'READY',
  REGISTERING = 'REGISTERING',
  UPGRADE = 'UPGRADE',
  UPGRADED = 'UPGRADED'
}

export interface NodeListDetails {
  instanceName: string;
  instanceType: string;
  ip: string;
  nodeName: string;
  region: string;
  sshUser: string;
  zone: string;
}

export interface ProviderNode {
  details: NodeListDetails;
  detailsJson: any;
  inUse: boolean;
  instanceName: string;
  instanceTypeCode: string;
  nodeName: string;
  nodeUuid: string;
  zoneUuid: string;
}

export interface TaskResponse {
  taskUUID: string;
}

export interface MetadataFields {
	id: string;
	name?: string;
	customerId: string;
	apiToken: string;
	platformUrl: string;
	metricsUrl: string;
	metricsScrapePeriodSec: number;
	dataMountPoints: string[];
	otherMountPoints: string[];
	lastSyncError?: string | null;
}

export interface UpdateMetadataFormFields {
	apiToken: string;
	metricsScrapePeriodSec: number;
}
