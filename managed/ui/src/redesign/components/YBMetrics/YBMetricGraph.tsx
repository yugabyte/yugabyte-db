import { ReactNode } from 'react';
import { Box, useTheme } from '@material-ui/core';
import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ReferenceLine,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis
} from 'recharts';

import { CHART_RESIZE_DEBOUNCE } from '../../helpers/constants';
import { YBMetricGraphTitle } from './YBMetricGraphTitle';
import { formatDatetime, YBTimeFormats } from '../../helpers/DateUtils';
import { YBMetricGraphData } from './types';
import { getUniqueTraceId, getUniqueTraceName } from './utils';

import { MetricSettings, MetricTrace } from '../../helpers/dtos';

interface YBMetricGraphCommonProps {
  metric: YBMetricGraphData;
  title: string;
  metricSettings: MetricSettings;
  referenceLines?: {
    y: number;
    name: string;
    strokeColor: string;
    ifOverflow?: 'hidden' | 'visible' | 'discard' | 'extendDomain';
  }[];

  graphHeaderAccessor?: ReactNode;
}

type YBMetricGraphProps =
  | (YBMetricGraphCommonProps & {
      namespaceUuidToNamespaceName: { [namespaceUuid: string]: string };

      getUniqueTraceNameOverride?: never;
    })
  | (YBMetricGraphCommonProps & {
      getUniqueTraceNameOverride: (metricSettings: MetricSettings, trace: MetricTrace) => string;

      namespaceUuidToNamespaceName?: never;
    });

export const YBMetricGraph = (props: YBMetricGraphProps) => {
  const { graphHeaderAccessor, metric, metricSettings, referenceLines, title } = props;
  const theme = useTheme();
  const traceStrokes = Object.values(theme.palette.chart.stroke);

  return (
    <Box display="flex" flexDirection="column" justifyContent="center" width="100%" height="600px">
      <Box display="flex" justifyContent="space-between">
        <YBMetricGraphTitle
          title={title}
          metricsLinkUseBrowserFqdn={metric.metricsLinkUseBrowserFqdn}
          directUrls={metric.directURLs}
        />
        <Box display="flex" gridGap={theme.spacing(2)}>
          {graphHeaderAccessor}
        </Box>
      </Box>
      <ResponsiveContainer width="100%" height="100%" debounce={CHART_RESIZE_DEBOUNCE}>
        <LineChart
          data={metric.metricData}
          margin={{
            top: theme.spacing(3),
            bottom: theme.spacing(2),
            left: theme.spacing(2),
            right: theme.spacing(2)
          }}
        >
          <CartesianGrid strokeDasharray="3 3" />
          <XAxis
            dataKey="x"
            tickFormatter={(value) => formatDatetime(value, YBTimeFormats.YB_TIME_ONLY_TIMESTAMP)}
            type="number"
            domain={['dataMin', 'dataMax']}
          />
          <YAxis tickFormatter={(value) => `${value} ms`} />
          <Tooltip labelFormatter={(value) => formatDatetime(value)} isAnimationActive={false} />
          <Legend iconType="plainline" />
          {metric.metricTraces.map((trace, index) => {
            const timeSeriesKey = getUniqueTraceId(trace);
            const timeSeriesName = props.getUniqueTraceNameOverride
              ? props.getUniqueTraceNameOverride(metricSettings, trace)
              : getUniqueTraceName(metricSettings, trace, props.namespaceUuidToNamespaceName);
            return (
              <Line
                key={timeSeriesKey}
                name={timeSeriesName}
                type="linear"
                dataKey={timeSeriesKey}
                stroke={traceStrokes[index]}
                unit=" ms"
              />
            );
          })}
          {referenceLines &&
            referenceLines.length > 0 &&
            referenceLines.map((referenceLine) => (
              <>
                {/* Line component with no dataKey used to add the reference line in the legend.
                      Recharts doesn't provide an option to add reference lines to the legend directly. */}
                <Line
                  name={referenceLine.name}
                  stroke={referenceLine.strokeColor}
                  strokeDasharray="4 4"
                />
                <ReferenceLine
                  y={referenceLine.y}
                  stroke={referenceLine.strokeColor}
                  ifOverflow={referenceLine.ifOverflow}
                  strokeDasharray="4 4"
                />
              </>
            ))}
        </LineChart>
      </ResponsiveContainer>
    </Box>
  );
};
