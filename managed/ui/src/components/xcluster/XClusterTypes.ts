import moment from 'moment';

import {
  CUSTOM_METRIC_TIME_RANGE_OPTION,
  DROPDOWN_DIVIDER,
  MetricName,
  METRIC_TIME_RANGE_OPTIONS,
  ReplicationStatus,
  XClusterTableStatus
} from './constants';

import { TableType, YBTable } from '../../redesign/helpers/dtos';

/**
 * XCluster supported table type.
 */
export type XClusterTableType = TableType.PGSQL_TABLE_TYPE | TableType.YQL_TABLE_TYPE;

export interface XClusterConfig {
  createTime: string;
  modifyTime: string;
  name: string;
  paused: boolean;
  sourceUniverseUUID: string;
  status: ReplicationStatus;
  tableDetails: XClusterTableDetails[];
  tables: string[];
  targetUniverseUUID: string;
  uuid: string;
}

/**
 * Source: XClusterTableConfig.java
 */
export interface XClusterTableDetails {
  needBootstrap: boolean;
  replicationSetupDone: true;
  bootstrapCreateTime: string;
  status: XClusterTableStatus;
  restoreTime: string;
  streamId: string;
  tableId: string;
}

export type XClusterTable = YBTable & Omit<XClusterTableDetails, 'tableId'>;

// TODO: Move the metric types to dtos.ts or another more appropriate file.

export interface MetricTrace {
  instanceName?: string;
  name: string;
  type: string;
  x: number[];
  y: string[] | number[];
  mode?: string;
  line?: {
    dash: string;
    width: number;
  };
}

export type Metrics<MetricNameType extends MetricName> = {
  [metricName in MetricNameType]: {
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
};

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
