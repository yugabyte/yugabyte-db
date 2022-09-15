import moment from 'moment';
import { TableType } from '../../redesign/helpers/dtos';
import {
  CUSTOM_METRIC_TIME_RANGE_OPTION,
  DROPDOWN_DIVIDER,
  METRIC_TIME_RANGE_OPTIONS,
  ReplicationStatus
} from './constants';

export interface ReplicationTable {
  tableUUID: string;
  pgSchemaName: string;
  tableName: string;
  tableType: TableType;
  keySpace: string;
  sizeBytes: string;
}

export interface Replication {
  createTime: string;
  modifyTime: string;
  name: string;
  paused: boolean;
  sourceUniverseUUID: string;
  status: ReplicationStatus;
  tableDetails: TableDetails[];
  tables: string[];
  targetUniverseUUID: string;
  uuid: string;
}

export interface TableDetails {
  needBootstrap: boolean;
  replicationSetupDone: true;
  streamId: string;
  tableId: string;
}

export interface MetricTrace {
  instanceName: string;
  name: string;
  type: string;
  x: number[];
  y: number[];
  mode?: string;
  line?: {
    dash: string;
    width: number;
  };
}

// TODO - Make this more robust and reusable
export interface TableReplicationMetric {
  tserver_async_replication_lag_micros: {
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
    queryKey: string;
  };
}

// Time range selector types.

export type MetricTimeRangeOption = Exclude<
  typeof METRIC_TIME_RANGE_OPTIONS[number],
  typeof DROPDOWN_DIVIDER
>;

export type StandardMetricTimeRangeOption = Exclude<
  MetricTimeRangeOption,
  typeof CUSTOM_METRIC_TIME_RANGE_OPTION
>;

export interface MetricTimeRange {
  startMoment: moment.Moment;
  endMoment: moment.Moment;
}
