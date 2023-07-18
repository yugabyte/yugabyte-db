// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import PropTypes from 'prop-types';
import { YBLoading } from '../../common/indicators';
import { METRIC_COLORS } from '../MetricsConfig';
import { YBWidget } from '../../panels';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';

class StandaloneMetricsPanel extends Component {
  static propTypes = {
    type: PropTypes.string.isRequired,
    metricKey: PropTypes.string.isRequired,

    /**
     * An optional array of objects containing additional metrics that will be concatenated onto the current
     * metricKey's data property.
     *
     * @type {Array} additionalMetricKeys
     * @property {Object} renameMetricObj           - Object in `additionalMetricKeys` that specifies a
     *                                                mapping from the original metric to the new one.
     * @property {string} renameMetricObj.metric    - String of a metric key found in `metrics[type]`.
     * @property {string} renameMetricObj.name      - String that will replace the name found in metric object.
     */
    additionalMetricKeys: PropTypes.array,
    width: PropTypes.number,
    children: PropTypes.func.isRequired
  };

  render() {
    const {
      type,
      graph: { metrics, masterMetrics },
      metricKey,
      additionalMetricKeys,
      isDedicatedNodes,
      width
    } = this.props;
    const props = {};
    props.metricKey = metricKey;
    props.width = width;

    if (
      metrics &&
      Object.keys(metrics).length > 0 &&
      isNonEmptyObject(metrics[type]) &&
      isNonEmptyObject(metrics[type][metricKey]) &&
      !metrics[type][metricKey].error
    ) {
      /* Logic here is that some other main component
      like OverviewMetrics or CustomerMetricsPanel is capabale of loading data
      and this panel only displays relevant data for the metric
      that should be displayed separately
      */
      const legendData = [];
      for (let idx = 0; idx < metrics[type][metricKey].data.length; idx++) {
        metrics[type][metricKey].data[idx].fill = 'tozeroy';
        metrics[type][metricKey].data[idx].fillcolor = METRIC_COLORS[idx] + '10';
        metrics[type][metricKey].data[idx].line = {
          color: METRIC_COLORS[idx],
          width: 1.5
        };
        legendData.push({
          color: METRIC_COLORS[idx],
          title: metrics[type][metricKey].data[idx].name
        });
      }

      props.metric = metrics[type][metricKey];

      if (additionalMetricKeys?.length) {
        additionalMetricKeys.forEach((info) => {
          if (metrics[type][info.metric]) {
            // Get the first element and rename
            const renamedMetric = { ...metrics[type][info.metric].data[0] };
            renamedMetric.name = info.name;
            const existingIndex = props.metric.data.findIndex((x) => x.name === info.name);
            if (existingIndex > -1) {
              props.metric.data[existingIndex] = renamedMetric;
            } else {
              props.metric.data.push(renamedMetric);
            }
          }
        });
      }
    } else {
      props.metric = {
        data: [],
        layout: { title: metricKey, xaxis: {}, yaxis: {} },
        queryKey: metricKey
      };
      return <YBWidget noMargin body={<YBLoading />} />;
    }

    if (isDedicatedNodes) {
      if (
        masterMetrics &&
        Object.keys(masterMetrics).length > 0 &&
        isNonEmptyObject(masterMetrics[type]) &&
        isNonEmptyObject(masterMetrics[type][metricKey]) &&
        !masterMetrics[type][metricKey].error
      ) {
        /* Logic here is that some other main component
      like OverviewMetrics or CustomerMetricsPanel is capabale of loading data
      and this panel only displays relevant data for the metric
      that should be displayed separately
      */
        const legendData = [];
        for (let idx = 0; idx < masterMetrics[type][metricKey].data.length; idx++) {
          masterMetrics[type][metricKey].data[idx].fill = 'tozeroy';
          masterMetrics[type][metricKey].data[idx].fillcolor = METRIC_COLORS[idx] + '10';
          masterMetrics[type][metricKey].data[idx].line = {
            color: METRIC_COLORS[idx],
            width: 1.5
          };
          legendData.push({
            color: METRIC_COLORS[idx],
            title: masterMetrics[type][metricKey].data[idx].name
          });
        }

        props.masterMetric = masterMetrics[type][metricKey];

        if (additionalMetricKeys && additionalMetricKeys.length) {
          additionalMetricKeys.forEach((info) => {
            if (masterMetrics[type][info.metric]) {
              // Get the first element and rename
              const renamedMetric = { ...masterMetrics[type][info.metric].data[0] };
              renamedMetric.name = info.name;
              const existingIndex = props.masterMetric.data.findIndex((x) => x.name === info.name);
              if (existingIndex > -1) {
                props.masterMetric.data[existingIndex] = renamedMetric;
              } else {
                props.masterMetric.data.push(renamedMetric);
              }
            }
          });
        }
      } else {
        props.masterMetric = {
          data: [],
          layout: { title: metricKey, xaxis: {}, yaxis: {} },
          queryKey: metricKey
        };
        return <YBWidget noMargin body={<YBLoading />} />;
      }
    }

    return this.props.children(props);
  }
}

export default StandaloneMetricsPanel;
