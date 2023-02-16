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

export interface CpuMeasureQueryData {
  maxNodeName: string;
  maxNodeValue: number;
  otherNodesAvgValue: number;
}
export interface CpuMeasureRecommendation {
  data: CpuMeasureQueryData;
  summary: React.ReactNode | string;
}

export interface CpuUsageRecommendation {
  summary: React.ReactNode | string;
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
  maxNodeName: string;
  percentDiff: number;
  maxNodeDistribution: NodeDistributionData;
  otherNodesDistribution: NodeDistributionData;
}

export interface QueryLoadRecommendation {
  data: QueryLoadData;
  summary: React.ReactNode | string;
}

export enum RecommendationTypeEnum {
  All = 'All',
  SchemaSuggestion = 'SchemaSuggestion',
  QueryLoadSkew = 'QueryLoadSkew',
  IndexSuggestion = 'IndexSuggestion',
  ConnectionSkew = 'ConnectionSkew',
  CpuSkew = 'CpuSkew',
  CpuUsage = 'CpuUsage'
}
