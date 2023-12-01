import React, { FC, ReactNode, useState, useEffect, useCallback } from 'react';
import { useUpdateEffect } from 'react-use';
import { getUnixTime } from 'date-fns';
import { useTranslation, TFuncKey, Namespace } from 'react-i18next';
import { MetricResponse, useGetClusterMetricQuery } from '@app/api/src';
import { getInterval, RelativeInterval, roundDecimal, timeFormatterWithStartEnd } from '@app/helpers';
import { YBChartContainer } from '@app/components/YBChart/YBChartContainer';
import { YBLineChartOptions, YBLinerChart } from '@app/components/YBChart/YBLinerChart';
import { useQueryParam } from 'use-query-params';

interface ChartDataPoint {
  time: number;
  ['CPU_USAGE_SYSTEM']?: number | null;
  ['CPU_USAGE_USER']?: number | null;
  ['READ_OPS_PER_SEC']?: number | null;
  ['WRITE_OPS_PER_SEC']?: number | null;
  ['AVERAGE_READ_LATENCY_MS']?: number | null;
  ['AVERAGE_WRITE_LATENCY_MS']?: number | null;
  ['DISK_USAGE_GB']?: number | null;
  ['PROVISIONED_DISK_SPACE_GB']?: number | null;
  ['TOTAL_LIVE_NODES']?: number | null;
}

interface ChartContainerProps {
  title: ReactNode;
  chartDrawingType: ('line' | 'area' | 'bar')[];
  relativeInterval: RelativeInterval;
  metric: string | string[];
  nodeName?: string;
  metricChartLabels: string | string[];
  strokes?: string[];
  unitKey?: TFuncKey<Namespace>;
  refreshFromParent?: number;
  regionName?: string;
  zone?: string;
}

/*
  Convert API response with potentially multiple metrics into array of data points suitable for chart

  API response:
    [
      { "name": "WRITE_OPS_PER_SEC", "values": [[1627408800, 8], [1627419600, 6], ...] },
      { "name": "READ_OPS_PER_SEC", "values": [[1627408800, 42], [1627419600, 37], ...] }
    ]

  Chart data:
    [
      { "time": 1627408800, "WRITE_OPS_PER_SEC": 8, "READ_OPS_PER_SEC": 42 },
      { "time": 1627419600, "WRITE_OPS_PER_SEC": 6, "READ_OPS_PER_SEC": 37 },
      ...
    ]
*/
const transformResponse = (response?: MetricResponse): ChartDataPoint[] => {
  const dataMap: Record<number, ChartDataPoint> = {};
  response?.data.forEach((metric) => {
    metric.values.forEach(([time, value]) => {
      if (dataMap[time]) {
        dataMap[time] = { ...dataMap[time], [metric.name]: value };
      } else {
        dataMap[time] = { time, [metric.name]: value };
      }
    });
  });

  return Object.values(dataMap);
};

export const ChartController: FC<ChartContainerProps> = ({
  title,
  relativeInterval,
  metric,
  nodeName,
  metricChartLabels,
  strokes,
  chartDrawingType,
  unitKey,
  refreshFromParent,
  regionName,
  zone,
}) => {
  const [interval, setNewInterval] = useState(() => getInterval(relativeInterval));
  const { t } = useTranslation();
  const [newNodeName, setNewNodeName] = useState(nodeName === 'all' ? undefined : nodeName);
  const { data: chartData, isLoading, isError } = useGetClusterMetricQuery<ChartDataPoint[]>(
    {
      metrics: Array.isArray(metric) ? metric.join(',') : metric,
      node_name: newNodeName,
      start_time: getUnixTime(interval.start),
      end_time: getUnixTime(interval.end),
      region: regionName === '' ? undefined : regionName,
      zone: zone === '' ? undefined : zone,
    },
    {
      query: {
        select: transformResponse
      }
    }
  );

  // return data type of the query is hardcoded in auto-generated code, use hacks to override it
  const data = (chartData as unknown) as ChartDataPoint[];

  // update start/end time on relative interval prop change only
  useUpdateEffect(() => {
    setNewInterval(getInterval(relativeInterval));
    setNewNodeName(nodeName === 'all' ? undefined : nodeName);
  }, [relativeInterval, nodeName]);

  const [ refreshChartController, setRefreshChartController ] =
    useQueryParam<boolean | undefined>("refreshChartController");

  // getInterval() will return new timestamps on every call which will trigger query re-run
  const refresh = useCallback(() => {
    setNewInterval(getInterval(relativeInterval));
    setRefreshChartController(undefined, "replaceIn");
  }, [relativeInterval]);

  useEffect(() => {
    refresh();
  }, [refreshFromParent, refresh, refreshChartController]);

  const tooltipFormatter = (value: number, name: string) => {
    const tooltipVal = roundDecimal(value).toLocaleString();
    return [unitKey ? t(unitKey, { value: tooltipVal }).toString() : tooltipVal, `● ${name}: `];
  };

  const chartOptions: YBLineChartOptions = {
    dataKeys: Array.isArray(metric) ? metric : [metric],
    lineLabels: Array.isArray(metricChartLabels) ? metricChartLabels : [metricChartLabels],
    chartDrawingType,
    strokes,
    xAxisDataKey: 'time',
    xAxisTickFormatter: timeFormatterWithStartEnd(interval.start, interval.end),
    toolTip: {
      isVisible: true,
      formatter: tooltipFormatter,
      labelFormatter: timeFormatterWithStartEnd(interval.start, interval.end)
    },
    legend: {
      visible: true,
      isSelectable: true
    }
  };

  return (
    <YBChartContainer title={title} isLoading={isLoading} isError={isError} onRefresh={refresh}>
      <YBLinerChart data={data} options={chartOptions} />
    </YBChartContainer>
  );
};
