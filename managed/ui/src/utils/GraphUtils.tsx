import { MetricMeasure, MetricTypes } from '../components/metrics/constants';
import { MetricsPanel } from '../components/metrics';
import { isKubernetesUniverse } from './UniverseUtils';
import { YBLoading, YBErrorIndicator } from '../components/common/indicators';
import { isNonEmptyObject, isNonEmptyString } from './ObjectUtils';

export const getTabContent = (
  graph: any,
  selectedUniverse: any,
  type: string,
  metricsKey: string[],
  title: string,
  currentUser: any,
  isGranularMetricsEnabled: boolean,
  updateTimestamp: (start: 'object' | number, end: 'object' | number) => void,
  printMode: boolean
) => {
  let tabData: any = <YBLoading />;
  if (graph.error?.data && !graph.loading) {
    return (
      <YBErrorIndicator
        customErrorMessage={
          isNonEmptyString(graph.error?.data?.error)
            ? graph.error?.data?.error
            : 'Error receiving response from Graph Server'
        }
      />
    );
  }

  const { metrics, prometheusQueryEnabled } = graph;
  const { nodeName, metricMeasure } = graph.graphFilter;

  if (Object.keys(metrics).length > 0 && isNonEmptyObject(metrics[type])) {
    /* Logic here is, since there will be multiple instances of GraphTab
      we basically would have metrics data keyed off tab type. So we
      loop through all the possible tab types in the metric data fetched
      and group metrics by panel type and filter out anything that is empty.
      */
    tabData = metricsKey
      .map(function (metricKey: string, idx: number) {
        let uniqueOperations: any = new Set();
        const metric = metrics[type][metricKey];
        if (
          (metricMeasure === MetricMeasure.OUTLIER || type === MetricTypes.OUTLIER_TABLES) &&
          isNonEmptyObject(metric)
        ) {
          metric.data.forEach((metricItem: any) => {
            uniqueOperations.add(metricItem.name);
          });
        }
        uniqueOperations = Array.from(uniqueOperations);

        return isNonEmptyObject(metric) && !metric?.error ? (
          <MetricsPanel
            currentUser={currentUser}
            metricKey={metricKey}
            // eslint-disable-next-line react/no-array-index-key
            key={`metric-${metricKey}-${idx}`}
            metric={metric}
            metricType={type}
            className={'metrics-panel-container'}
            containerWidth={null}
            prometheusQueryEnabled={prometheusQueryEnabled}
            metricMeasure={metricMeasure}
            operations={uniqueOperations}
            isGranularMetricsEnabled={isGranularMetricsEnabled}
            updateTimestamp={updateTimestamp}
            printMode={printMode}
          />
        ) : null;
      })
      .filter(Boolean);
  }

  if (selectedUniverse && isKubernetesUniverse(selectedUniverse)) {
    //Hide master related panels for tserver pods.
    // eslint-disable-next-line eqeqeq
    if (nodeName.match('yb-tserver-') != null) {
      if (title === 'Master Server' || title === 'Master Server Advanced') {
        return null;
      }
    }
    //Hide empty panels for master pods.
    // eslint-disable-next-line eqeqeq
    if (nodeName.match('yb-master-') != null) {
      const skipList = [
        'Tablet Server',
        'YSQL Ops',
        'YCQL Ops',
        'YEDIS Ops',
        'YEDIS Advanced',
        'Resource'
      ];
      if (skipList.includes(title)) {
        return null;
      }
    }
  }
  return tabData;
};
