import React from 'react';

import { MetricMeasure, MetricTypes } from '../components/metrics/constants';
import { MetricsPanel } from '../components/metrics';
import { isKubernetesUniverse } from './UniverseUtils';
import { YBLoading, YBErrorIndicator } from '../components/common/indicators';
import {
  isNonEmptyObject
} from './ObjectUtils';
import { YUGABYTE_TITLE } from '../config';

export const getTabContent = (
  graph: any,
  selectedUniverse: any,
  type: string,
  metricsKey: string[],
  title: string,
  currentUser: any,
  insecureLoginToken: any
) => {
  let tabData: any = <YBLoading />;
  if (graph.error?.data) {
    return <YBErrorIndicator customErrorMessage="Error receiving response from Graph Server"/>;
  }

  if (
    insecureLoginToken &&
    !(type === MetricTypes.YCQL_OPS || type === MetricTypes.YSQL_OPS || type === MetricTypes.YEDIS_OPS)
  ) {
    tabData = (
      <div className="oss-unavailable-warning">Only available on {YUGABYTE_TITLE}.</div>
    );
  }
  else {
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
          if (metricMeasure === MetricMeasure.OUTLIER && isNonEmptyObject(metric)) {
            metric.data.forEach((metricItem: any) => {
              uniqueOperations.add(metricItem.name);
            });
          }
          uniqueOperations = Array.from(uniqueOperations);
          return isNonEmptyObject(metric) && !metric?.error ? (
            <MetricsPanel
              currentUser={currentUser}
              metricKey={metricKey}
              key={`metric-${metricKey}-${idx}`}
              metric={metric}
              className={'metrics-panel-container'}
              containerWidth={null}
              prometheusQueryEnabled={prometheusQueryEnabled}
              metricMeasure={metricMeasure}
              operations={uniqueOperations}
            />
          ) : null;
        })
        .filter(Boolean);
    }

    if (selectedUniverse && isKubernetesUniverse(selectedUniverse)) {
      //Hide master related panels for tserver pods.
      if (nodeName.match('yb-tserver-') != null) {
        if (title === 'Master Server' || title === 'Master Server Advanced') {
          return null;
        }
      }
      //Hide empty panels for master pods.
      if (nodeName.match('yb-master-') != null) {
        const skipList = ['Tablet Server',
          'YSQL Ops',
          'YCQL Ops',
          'YEDIS Ops',
          'YEDIS Advanced',
          'Resource']
        if (skipList.includes(title)) {
          return null;
        }
      }
    }
  }
  return tabData;
}
