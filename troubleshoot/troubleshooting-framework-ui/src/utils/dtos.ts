export interface IndexAndShardingRecommendationData {
  type: RecommendationType;
  target: string;
  indicator: number;
  table: TableData;
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

const EntityType = {
  NODE: 'NODE',
  DATABASE: 'DATABASE',
  TABLE: 'TABLE',
  INDEX: 'INDEX',
  UNIVERSE: 'UNIVERSE'
} as const;
// eslint-disable-next-line no-redeclare
export type EntityType = typeof EntityType[keyof typeof EntityType];

const RecommendationPriority = {
  HIGH: 'HIGH',
  MEDIUM: 'MEDIUM',
  LOW: 'LOW'
} as const;
// eslint-disable-next-line no-redeclare
export type RecommendationPriority = typeof RecommendationPriority[keyof typeof RecommendationPriority];

const RecommendationState = {
  OPEN: 'OPEN',
  HIDDEN: 'HIDDEN',
  RESOLVED: 'RESOLVED'
} as const;
// eslint-disable-next-line no-redeclare
export type RecommendationState = typeof RecommendationState[keyof typeof RecommendationState];

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


interface HighestNodeQueryLoadDetails {
  DeleteStmt: number;
  InsertStmt: number;
  SelectStmt: number;
  UpdateStmt: number;
}

interface OtherNodeQueryLoadDetails extends HighestNodeQueryLoadDetails {}

export enum TroubleshootType {
  ALL = 'ALL',
  INFRA_ISSUE = 'INFRA_ISSUE',
  SQL_ISSUE = 'SQL_ISSUE',
  NODE_ISSUE = 'NODE_ISSUE',
  APP_ISSUE = 'APP_ISSUE'
}

export interface NodeIssueProps {
  data: QueryLoadData;
  summary: React.ReactNode | string;
  uuid?: string;
}

export interface QueryLoadData {
  suggestion: string;
  maxNodeName: string;
  percentDiff: number;
  maxNodeDistribution: NodeDistributionData;
  otherNodesDistribution: NodeDistributionData;
}

export interface NodeDistributionData {
  numSelect: number;
  numInsert: number;
  numUpdate: number;
  numDelete: number;
}

export enum MetricMeasure {
  OVERALL = 'Overall',
  OUTLIER = 'Outlier',
  OUTLIER_TABLES = 'Outlier_Tables'
}
