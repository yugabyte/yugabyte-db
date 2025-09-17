// Copyright (c) YugabyteDB, Inc.

import { Component, Fragment } from 'react';
import { Graph } from '../';
import { NodeType } from '../../../redesign/utils/dtos';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { YBResourceCount } from '../../../components/common/descriptors';

import './MetricsPanel.scss';

export default class CpuUsagePanel extends Component {
  static propTypes = {};

  render() {
    const {
      isKubernetes,
      metric,
      masterMetric,
      isDedicatedNodes,
      useK8CustomResources
    } = this.props;
    const usage = {
      system: undefined,
      user: undefined,
      total: undefined
    };

    const masterUsage = {
      system: undefined,
      user: undefined,
      total: undefined
    };

    try {
      if (isNonEmptyArray(metric.data)) {
        if (isKubernetes) {
          usage.total = parseFloat(
            metric.data.find((item) => item.name === 'cpu_usage').y.slice(-1)[0]
          );
        } else {
          usage.total = parseFloat(
            metric.data.find((item) => item.name === 'Total').y.slice(-1)[0]
          );
        }
      }

      if (masterMetric && isNonEmptyArray(masterMetric.data)) {
        if (isKubernetes) {
          masterUsage.total = parseFloat(
            masterMetric.data.find((item) => item.name === 'cpu_usage').y.slice(-1)[0]
          );
        } else {
          masterUsage.total = parseFloat(
            masterMetric.data.find((item) => item.name === 'Total').y.slice(-1)[0]
          );
        }
      }
    } catch (err) {
      console.error('CPU metric processing failed with: ' + err);
    }
    const value = usage.total
      ? Math.round(usage.total * 10) / 1000
      : usage.system
      ? Math.round(usage.system * 10) / 1000
      : 0;

    const masterValue = masterUsage.total
      ? Math.round(masterUsage.total * 10) / 1000
      : masterUsage.system
      ? Math.round(masterUsage.system * 10) / 1000
      : 0;
    const customClassName = isDedicatedNodes ? 'dedicated' : 'colocated';

    return (
      <div className={`metrics-padded-panel cpu-usage-panel ${customClassName}-mode-panel`}>
        {isNaN(usage.total) ? (
          <Fragment>
            {isKubernetes && useK8CustomResources && (
              <span className="node-type-label cpu">{NodeType.TServer}</span>
            )}
            <div
              className={`centered text-light text-lightgray empty-state ${customClassName}-mode-empty`}
            >
              No Data
            </div>
            <Graph value={0} />

            {isDedicatedNodes && (
              <>
                <div
                  className={`centered text-light text-lightgray empty-state ${customClassName}-mode-empty`}
                >
                  No Data
                </div>
                <Graph value={0} />
              </>
            )}
          </Fragment>
        ) : (
          <Fragment>
            <YBResourceCount
              size={Math.round(value * 1000) / 10}
              kind="% used"
              inline={true}
              label={
                (isKubernetes && useK8CustomResources) || isDedicatedNodes ? NodeType.TServer : null
              }
            />
            <Graph value={value} />
            {isDedicatedNodes && (
              <>
                <YBResourceCount
                  size={Math.round(masterValue * 1000) / 10}
                  kind="% used"
                  inline={true}
                  label={isDedicatedNodes ? NodeType.Master : null}
                />
                <Graph value={masterValue} />
              </>
            )}
          </Fragment>
        )}
      </div>
    );
  }
}
