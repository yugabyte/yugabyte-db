import React, { FC } from 'react';
import { Button, ButtonGroup } from 'react-bootstrap';

import { MetricMeasure } from '../../metrics/constants';
import treeIcon from '../../metrics/images/tree-icon.svg';
import './MetricsMeasureSelector.scss';

interface MetricMeasureSelectorData {
  onMetricMeasureChanged: any;
  selectedMetricMeasureValue: string | null;
  metricMeasureTypes: [{ value: string }];
  isSingleNodeSelected: boolean;
}

export const MetricsMeasureSelector: FC<MetricMeasureSelectorData> = ({
  metricMeasureTypes,
  selectedMetricMeasureValue,
  onMetricMeasureChanged,
  isSingleNodeSelected
}) => {
  const isOutlierDisabled = (metricMeasureValue: string) => {
    return metricMeasureValue === MetricMeasure.OUTLIER && isSingleNodeSelected;
  }

  return (
    <div className="metrics-measure-container">
      <img className="downright-arrow" src={treeIcon} alt="Indicator towards metric measure to use" />
      <span className="metrics-measure-content">
        <span className="metrics-measure-label">View metrics for</span>
        <ButtonGroup>
          {metricMeasureTypes.map((metricMeasureType: any, idx: number) => {
            return (
              <Button
                key={idx}
                onClick={() => onMetricMeasureChanged(metricMeasureType.value)}
                active={selectedMetricMeasureValue === metricMeasureType.value}
                className={`metrics-measure-button__${(metricMeasureType.value).toLowerCase()}`}
                disabled={isOutlierDisabled(metricMeasureType.value)}
                data-placement="right"
                title={isOutlierDisabled(metricMeasureType.value)
                  ? "Clear node selection to see outliers for each cluster, region, or availability zone (AZ)."
                  : ""}
              >
                <span className="metric-measure-button-title">{metricMeasureType.label}</span>
              </Button>
            );
          })
          }
        </ButtonGroup>
      </span>
    </div>
  );
}
