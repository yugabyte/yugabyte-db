import React, { FC } from 'react';
import { Button, ButtonGroup } from 'react-bootstrap';

import { MetricMeasure, MetricConsts } from '../../metrics/constants';
import treeIcon from '../../metrics/images/tree-icon.svg';
import './MetricsMeasureSelector.scss';

interface MetricMeasureSelectorData {
  onMetricMeasureChanged: any;
  selectedMetricMeasureValue: string | null;
  selectedNode: string | null;
  metricMeasureTypes: [{ value: string }];
  // Disable the Outlier button if user selects a AZ in node dropdown which has one node only
  isOneNodeInSelectedZone: boolean;
  isOneNodeInSelectedRegion: boolean;
}

export const MetricsMeasureSelector: FC<MetricMeasureSelectorData> = ({
  metricMeasureTypes,
  selectedMetricMeasureValue,
  onMetricMeasureChanged,
  selectedNode,
  isOneNodeInSelectedZone,
  isOneNodeInSelectedRegion
}) => {
  const isOutlierDisabled = (metricMeasureValue: string) => {
    return metricMeasureValue === MetricMeasure.OUTLIER && (
      selectedNode !== MetricConsts.ALL || isOneNodeInSelectedZone || isOneNodeInSelectedRegion);
  };

  return (
    <div className="metrics-measure-container">
      <img className="downright-arrow" src={treeIcon} alt="Indicator towards metric measure to use" />
      <span className="metrics-measure-content">
        <span className="metrics-measure-label">View metrics for</span>
        <ButtonGroup >
          {metricMeasureTypes.map((metricMeasureType: any, idx: number) => {
            return (
              <Button
                key={idx}
                onClick={() => onMetricMeasureChanged(metricMeasureType.value)}
                active={selectedMetricMeasureValue === metricMeasureType.value}
                disabled={isOutlierDisabled(metricMeasureType.value)}
                data-placement="right"
                title={isOutlierDisabled(metricMeasureType.value)
                  ? "Clear node selection to see outliers for each cluster, region, or availability zone (AZ)." : ""}
              >
                <span className="metric-measure-button-title">{metricMeasureType.value}</span>
              </Button>
            );
          })
          }
        </ButtonGroup>
      </span>
    </div>
  );
}
