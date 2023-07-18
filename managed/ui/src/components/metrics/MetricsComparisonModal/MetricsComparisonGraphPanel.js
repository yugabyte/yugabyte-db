import { useState, useContext } from 'react';
import './MetricsComparisonModal.scss';
import { Dropdown, Panel } from 'react-bootstrap';
import { FilterContext } from './ComparisonFilterContextProvider';
import { MetricsComparisonPanel } from './MetricsComparisonPanel';
import { FlexContainer } from '../../common/flexbox/YBFlexBox';
import { YBMenuItem } from '../../universes/UniverseDetail/compounds/YBMenuItem';
import ellipsisIcon from '../../common/media/more.svg';

export const MetricsComparisonGraphPanel = ({ metricsKey }) => {
  const [isOpen, setIsOpen] = useState(true);

  const [state, dispatch] = useContext(FilterContext);
  const {
    metricsData: { firstMetricsData, secondMetricsData }
  } = state;

  const removeMetrics = () => {
    const newMetrics = state.selectedMetrics.filter((metric) => {
      return metric !== metricsKey;
    });
    dispatch({
      type: 'CHANGE_SELECTED_METRICS',
      payload: newMetrics
    });
  };

  return (
    <Panel key={metricsKey} className="metrics-container" expanded={isOpen} onToggle={() => {}}>
      <Panel.Title className="metrics-comparison-panel-header">
        <i className="fa fa-caret-down open-icon" onClick={() => setIsOpen(!isOpen)} />
        {firstMetricsData[metricsKey]?.layout?.title}
        <Dropdown id="MetricsPanelDropdown" className="metrics-comparison-panel-dropdown" pullRight>
          <Dropdown.Toggle noCaret>
            <img src={ellipsisIcon} alt="more" className="ellipsis-icon" />
          </Dropdown.Toggle>
          <Dropdown.Menu>
            <YBMenuItem onClick={removeMetrics}>Remove</YBMenuItem>
          </Dropdown.Menu>
        </Dropdown>
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
