export interface Universe {
  creationDate: string;
  name: string;
  resources: Resources;
  universeConfig: UniverseConfig;
  universeDetails: any;
  universeUUID: string;
  version: number;
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

export enum MetricMeasure {
  OVERALL = 'Overall',
  OUTLIER = 'Outlier',
  OUTLIER_TABLES = 'Outlier_Tables'
}

export interface Anomaly {
  uuid: string;
  metadataUuid: string;
  category: AnomalyCategory;
  type: AnomalyType;
  universeUuid: string;
  affectedNodes: NodeInfo[];
  graphStartTime?: Date | string;
  graphEndTime?: Date | string;
  affectedTables?: TableInfo[] | [];
  title: string;
  summary: string;
  detectionTime: Date | string;
  startTime: Date | string;
  endTime?: Date | string;
  mainGraphs: GraphMetadata[];
  // supportingGraphs: GraphMetadata[];
  defaultSettings: GraphSettings;
  // commonGraphFilters: Map<string, Set<string>>;
  rcaGuidelines: RCAGuideline[];
}

export enum AnomalyCategory {
  SQL = 'SQL',
  APP = 'APP',
  NODE = 'NODE',
  INFRA='INFRA',
  DB='DB'
}

export enum AnomalyType {
  SQL_QUERY_LATENCY_INCREASE = 'SQL_QUERY_LATENCY_INCREASE',
  HOT_NODE_READS_WRITES = 'HOT_NODE_READS_WRITES',
  HOT_NODE_CPU = 'HOT_NODE_CPU',
  HOT_NODE_QUERIES = 'HOT_NODE_QUERIES',
  HOT_NODE_DATA = 'HOT_NODE_DATA',
  SLOW_DISKS = 'SLOW_DISKS'
}

export interface NodeInfo {
  name: string;
  uuid: string;
}

export interface TableInfo {
  databaseName: string;
  tableName: string;
  tableId: string;
}

export interface RCAGuideline {
  possibleCause: string;
  possibleCauseDescription: string;
  troubleshootingRecommendations: TroubleshootingRecommendations[];
}

export interface TroubleshootingRecommendations {
  recommendation: string;
  supportingGraphs: GraphMetadata[];
}

export interface GraphMetadata {
  name: string;
  // threshold?: number;
  filters: GraphFilters
}

export interface GraphQuery {
  start: Date | string;
  end: Date | string;
  name: string;
  filters: GraphFilters;
  settings: GraphSettings;
  groupBy?: GraphLabel[];
}

export enum GraphLabel {
  WAIT_EVENT_COMPONENT = 'waitEventComponent',
  WAIT_EVENT_TYPE = 'waitEventType',
  WAIT_EVENT_CLASS = 'waitEventClass',
  WAIT_EVENT_TYPE = 'waitEventType',
  WAIT_EVENT = 'waitEvent',
  CLIENT_NODE_IP = 'clientNodeIp',
  QUERY_ID = 'queryId'
}

export interface GraphFilters {
  universeUuid: string[];
  queryId?: string[];
  dbId?: string[];
  instanceName?: string[];
  clusterUuid?: string[];
  regionCode?: string[];
  azCode?: string[];
}

export interface GraphSettings {
  splitMode: SplitMode;
  splitType: SplitType;
  splitCount: number;
  returnAggregatedValue: boolean;
  aggregatedValueFunction: Aggregation;
}

export const SplitMode = {
  NONE: 'NONE',
  TOP: 'TOP',
  BOTTOM: 'BOTTOM'
} as const;
// eslint-disable-next-line no-redeclare
export type SplitMode = typeof SplitMode[keyof typeof SplitMode];

export const SplitType = {
  NONE: 'NONE',
  NODE: 'NODE',
  TABLE: 'TABLE',
  NAMESPACE: 'NAMESPACE'
} as const;
// eslint-disable-next-line no-redeclare
export type SplitType = typeof SplitType[keyof typeof SplitType];

export const Aggregation = {
  AVERAGE: 'AVG',
  MAX: 'MAX',
  MIN: 'MIN',
  SUM: 'SUM'
} as const;
// eslint-disable-next-line no-redeclare
export type Aggregation = typeof Aggregation[keyof typeof Aggregation];

export interface GraphResponse {
  successful: boolean;
  errorMessage: boolean;
  layout: any;
  data: any;
}

export enum AppName {
  YBA = 'YBA',
  YBM = 'YBM',
  YBD = 'YBD'
}

export enum GraphType {
  MAIN = 'MAIN',
  SUPPORTING = 'SUPPORTING'
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
