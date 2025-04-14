export interface HistogramBucket {
  [interval: string]: number;
}

export interface YsqlSlowQuery {
  calls: number;
  datname: string;
  local_blks_hit: number;
  local_blks_written: number;
  max_time: number;
  mean_time: number;
  min_time: number;
  P25: number | 'NaN';
  P50: number | 'NaN';
  P90: number | 'NaN';
  P95: number | 'NaN';
  P99: number | 'NaN';
  query: string;
  queryid: number;
  rows: number;
  stddev_time: number;
  total_time: number;
  rolname: string;
  min_plan_time: number;
  max_plan_time: number;
  mean_plan_time: number;
  stddev_plan_time: number;
  total_plan_time: number;
  yb_latency_histogram: HistogramBucket[];
}

export interface YsqlLiveQuery {
  appName: string;
  clientHost: string;
  clientPort: string;
  dbName: string;
  elapsedMillis: number;
  id: string;
  nodeName: string;
  privateIp: string;
  query: string;
  queryStartTime: string;
  sessionStatus: string;
  leader_pid: number;
  query_id: number;
}

export interface YcqlLiveQuery {
  clientHost: string;
  clientPort: string;
  keyspace: string;
  elapsedMillis: number;
  id: string;
  nodeName: string;
  privateIp: string;
  query: string;
  type: string;
}

export type YsqlSlowQueryPrimitiveFields = keyof Omit<YsqlSlowQuery, 'yb_latency_histogram'>;
export type YsqlLiveQueryPrimitiveFields = keyof YsqlLiveQuery;
export type YcqlLiveQueryPrimitiveFields = keyof YcqlLiveQuery;

export type QueryResponseKeys = keyof YcqlLiveQuery | keyof YsqlLiveQuery | keyof YsqlSlowQuery;
