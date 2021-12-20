import React, { useState, useContext } from 'react';
import './MetricsComparisonModal.scss';
import { Panel } from 'react-bootstrap';
import { FilterContext } from './ComparisonFilterContextProvider';
import { MetricsComparisonPanel } from './MetricsComparisonPanel';
import { FlexContainer } from '../../common/flexbox/YBFlexBox';

export const MetricsComparisonGraphPanel = ({ metricsKey }) => {
  const [isOpen, setIsOpen] = useState(true);

  const [state] = useContext(FilterContext);
  const {
    metricsData: { firstMetricsData, secondMetricsData }
  } = state;

  return (
    <Panel
      key={metricsKey}
      className="metrics-container"
      expanded={isOpen}
      onToggle={() => {}}
    >
      <Panel.Title className="metrics-comparison-panel-header" onClick={() => setIsOpen(!isOpen)}>
        <i className="fa fa-caret-down open-icon" />
        {firstMetricsData[metricsKey]?.layout?.title}
      </Panel.Title>
      <Panel.Collapse>
        <Panel.Body>
          <FlexContainer>
            <MetricsComparisonPanel
              side="left"
              key={'left-' + metricsKey}
              metricsKey={metricsKey}
              metricsData={firstMetricsData[metricsKey]?.data}
              metricsLayout={firstMetricsData[metricsKey]?.layout}
            />
            <MetricsComparisonPanel
              side="right"
              key={'right-' + metricsKey}
              metricsKey={metricsKey}
              metricsData={secondMetricsData[metricsKey]?.data}
              metricsLayout={secondMetricsData[metricsKey]?.layout}
            />
          </FlexContainer>
        </Panel.Body>
      </Panel.Collapse>
    </Panel>
  );
};
