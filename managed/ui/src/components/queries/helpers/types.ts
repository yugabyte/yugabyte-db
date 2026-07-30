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

/**
 * YSQL row from the live_queries API (merged from tserver /rpcz via `LiveQueryExecutor` in YBA).
 *
 * Rpcz JSON omits several keys conditionally (see `ybc_pg_webserver_wrapper.cc` in the DB repo).
 * YBA always sends `query` as a string (empty when rpcz had no query text).
 */
export interface YsqlLiveQuery {
  id: string;
  nodeName: string;
  privateIp: string;
  query: string;
  elapsedMillis: number;
  sessionStatus: string;
  appName: string;

  /** Rpcz omits db_oid/db_name together when db_oid is 0. */
  dbName?: string | null;
  /** Rpcz omits when no client host/port; hidden in UI when connection pooling is enabled. */
  clientHost?: string | null;
  clientPort?: string | null;
  /** May be absent when rpcz has no query_start_timestamp. */
  queryStartTime?: string | null;

  /** PG15+; omitted from the YBA row when rpcz does not send leader_pid. */
  leader_pid?: number;
  /** PG15+; omitted when rpcz omits query text (paired with query in tserver). */
  query_id?: string;
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
