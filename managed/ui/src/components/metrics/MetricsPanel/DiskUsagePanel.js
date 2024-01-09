// Copyright (c) YugaByte, Inc.

import { Component, Fragment } from 'react';
import { Graph } from '../';
import { NodeType } from '../../../redesign/utils/dtos';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';

import './MetricsPanel.scss';

export default class DiskUsagePanel extends Component {
  static propTypes = {};

  render() {
    const {
      metric,
      masterMetric,
      isDedicatedNodes,
      isKubernetes,
      useK8CustomResources
    } = this.props;
    const space = {
      free: undefined,
      used: undefined,
      size: undefined
    };

    const masterSpace = {
      free: undefined,
      used: undefined,
      size: undefined
    };
    if (isNonEmptyArray(metric.data)) {
      const diskUsedObj = metric.data.find((item) => item.name === 'used');
      const diskFreeObj = metric.data.find((item) => item.name === 'free');
      const diskSizeObj = metric.data.find((item) => item.name === 'size');
      const getLastElement = (arr) => arr?.length && arr[arr.length - 1];

      // If at least two out of three objects are defined we can figure out the rest
      if (!!diskUsedObj && !!diskFreeObj) {
        space.used = getLastElement(diskUsedObj.y);
        space.free = getLastElement(diskFreeObj.y);
        space.size = space.used + space.free;
      } else if (!!diskUsedObj && !!diskSizeObj) {
        space.used = getLastElement(diskUsedObj.y);
        space.size = getLastElement(diskSizeObj.y);
        space.free = space.size - space.used;
      } else if (!!diskFreeObj && !!diskSizeObj) {
        space.free = getLastElement(diskFreeObj.y);
        space.size = getLastElement(diskSizeObj.y);
        space.used = space.size - space.free;
      } else {
        console.error(
          `Metric missing properties. Free: ${diskFreeObj}, Used: ${diskUsedObj}, Size: ${diskSizeObj}`
        );
      }
    }

    if (masterMetric && isNonEmptyArray(masterMetric.data)) {
      const diskUsedObj = masterMetric.data.find((item) => item.name === 'used');
      const diskFreeObj = masterMetric.data.find((item) => item.name === 'free');
      const diskSizeObj = masterMetric.data.find((item) => item.name === 'size');
      const getLastElement = (arr) => arr && arr.length && arr[arr.length - 1];

      // If at least two out of three objects are defined we can figure out the rest
      if (!!diskUsedObj && !!diskFreeObj) {
        masterSpace.used = getLastElement(diskUsedObj.y);
        masterSpace.free = getLastElement(diskFreeObj.y);
        masterSpace.size = masterSpace.used + masterSpace.free;
      } else if (!!diskUsedObj && !!diskSizeObj) {
        masterSpace.used = getLastElement(diskUsedObj.y);
        masterSpace.size = getLastElement(diskSizeObj.y);
        masterSpace.free = masterSpace.size - masterSpace.used;
      } else if (!!diskFreeObj && !!diskSizeObj) {
        masterSpace.free = getLastElement(diskFreeObj.y);
        masterSpace.size = getLastElement(diskSizeObj.y);
        masterSpace.used = masterSpace.size - masterSpace.free;
      } else {
        console.error(
          `Master Metric missing properties. Free: ${diskFreeObj}, Used: ${diskUsedObj}, Size: ${diskSizeObj}`
        );
      }
    }

    const value = space.size ? Math.round((space.used * 1000) / space.size) / 1000 : 0;
    const masterValue = masterSpace.size
      ? Math.round((masterSpace.used * 1000) / masterSpace.size) / 1000
      : 0;
    const customClassName = isDedicatedNodes ? 'dedicated' : null;

    return (
      <div className={`metrics-padded-panel disk-usage-panel  ${customClassName}-mode-panel`}>
        {isNaN(space.size) ? (
          <Fragment>
            <div
              className={`centered text-light text-lightgray empty-state ${customClassName}-mode-empty`}
            >
              {isKubernetes && useK8CustomResources && (
                <span className="node-type-label disk">{NodeType.TServer}</span>
              )}
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
            <div className={'tserver-section'}>
              {space.used && (
                <>
                  <span className={`gray-text metric-left-subtitle ${customClassName}-mode-space`}>
                    {(isKubernetes && useK8CustomResources) ||
                      (isDedicatedNodes && (
                        <span className={'metric-left-subtitle__label'}>{NodeType.TServer}</span>
                      ))}
                    {(value * 100).toFixed(1)}%
                  </span>
                </>
              )}
              <span className="gray-text metric-right-subtitle">
                {Number(space.used).toFixed(2)} GB of {Math.round(space.size)} GB used
              </span>
              <Graph value={value} unit={'percent'} />
            </div>

            <div className={'master-section'}>
              {isDedicatedNodes && (
                <>
                  {masterSpace.used && (
                    <span
                      className={`gray-text metric-left-subtitle ${customClassName}-mode-space`}
                    >
                      <span className={'metric-left-subtitle__label'}>{NodeType.Master}</span>
                      {(masterValue * 100).toFixed(1)}%
                    </span>
                  )}
                  <span className="gray-text metric-right-subtitle">
                    {Number(masterSpace.used).toFixed(2)} GB of {Math.round(masterSpace.size)} GB
                    used
                  </span>
                  <Graph value={masterValue} unit={'percent'} />
                </>
              )}
            </div>
          </Fragment>
        )}
      </div>
    );
  }
}
