import { Metric, MetricTrace } from '../../helpers/dtos';

// We mark the fields of `Metric` as optional because these fields will be undefined
// if the desired metric is not found in the YBA api response.
export interface YBMetricGraphData extends Partial<Omit<Metric, 'data'>> {
  metricData: { [dataKey: string]: number | null }[];
  metricTraces: MetricTrace[];
}
