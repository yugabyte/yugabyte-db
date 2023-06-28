export interface HistogramBucket {
  [interval: string]: number;
}

export interface YSQLSlowQuery {
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
  userid: string;
  yb_latency_histogram: HistogramBucket[];
}
export type YSQLSlowQueryPrimativeFields = keyof Omit<YSQLSlowQuery, 'yb_latency_histogram'>;
