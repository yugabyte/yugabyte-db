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

export type YsqlSlowQueryPrimativeFields = keyof Omit<YsqlSlowQuery, 'yb_latency_histogram'>;
export type YsqlLiveQueryPrimativeFields = keyof YsqlLiveQuery;
export type YcqlLiveQueryPrimativeFields = keyof YcqlLiveQuery;

export type QueryResponseKeys = keyof YcqlLiveQuery | keyof YsqlLiveQuery | keyof YsqlSlowQuery;
