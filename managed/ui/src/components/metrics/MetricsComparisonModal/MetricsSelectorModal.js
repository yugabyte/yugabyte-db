import { useState, useContext, useEffect } from 'react';
import { YBCheckBox, YBModal } from '../../common/forms/fields';
import './MetricsComparisonModal.scss';
import { Col, Panel, Row } from 'react-bootstrap';
import { MetricTypesWithOperations } from '../../metrics/constants';
import { FilterContext } from './ComparisonFilterContextProvider';
import { Input } from '../../../redesign/uikit/Input/Input';
import { isKubernetesUniverse } from '../../../utils/UniverseUtils';

export const MetricsSelectorModal = ({ visible, onHide, selectedUniverse }) => {
  const [state, dispatch] = useContext(FilterContext);
  const [selectedMetrics, setSelectedMetrics] = useState(state.selectedMetrics);
  const [searchString, setSearchString] = useState('');
  const [metricsToDisplay, setMetricsToDisplay] = useState({});

  const handleFilterCheckboxClick = (filterText) => {
    if (selectedMetrics.includes(filterText)) {
      setSelectedMetrics(
        selectedMetrics.filter((val) => {
          return val !== filterText;
        })
      );
    } else {
      setSelectedMetrics([...selectedMetrics, filterText]);
    }
  };

  const handleSearchString = (event) => {
    setSearchString(event.target.value);
  };

  const getFilterCheckbox = (filter_text, handleFilterCheckboxClick) => (
    <YBCheckBox
      input={{ autoComplete: 'off' }}
      checkState={selectedMetrics.includes(filter_text)}
      onClick={() => handleFilterCheckboxClick(filter_text)}
      label={<span className="checkbox-label">{filter_text}</span>}
    />
  );

  useEffect(() => {
    // First, create a dictionary containing the only the metrics matching search string
    //  so that the headings with no matching metrics don't appear
    const lowerSearchString = searchString.replace('_', '').toLowerCase();
    const newMetricsToDisplay = {};
    Object.keys(MetricTypesWithOperations).forEach((key) => {
      const invalidPanelType =
        selectedUniverse && isKubernetesUniverse(selectedUniverse)
          ? MetricTypesWithOperations[key].title === 'Node'
          : MetricTypesWithOperations[key].title === 'Container';
      if (!invalidPanelType) {
        MetricTypesWithOperations[key].metrics.forEach((filter) => {
          if (filter.replace('_', '').toLowerCase().includes(lowerSearchString)) {
            if (!Object.prototype.hasOwnProperty.call(newMetricsToDisplay, key)) {
              newMetricsToDisplay[key] = [];
            }
            newMetricsToDisplay[key].push(filter);
          }
        });
      }
    });
    setMetricsToDisplay(newMetricsToDisplay);
  }, [searchString, selectedUniverse]);

  const submitSelectedMetrics = () => {
    dispatch({
      type: 'CHANGE_SELECTED_METRICS',
      payload: selectedMetrics
    });
    onHide();
  };

  return (
    <div className="metrics-comparison-modal">
      <YBModal
        visible={visible}
        onHide={onHide}
        submitLabel="Add"
        onFormSubmit={submitSelectedMetrics}
        title="Add Metrics to Compare"
        formClassName="metrics-selector-form"
        dialogClassName="metrics-selector-modal"
        size="large"
      >
        <Panel className="metrics-options-panel">
          <div className="metrics-options-title">Select up to 5 metrics to compare</div>
          <Input
            placeholder="Search metrics"
            className="metrics-selector-search-bar"
            value={searchString}
            onChange={handleSearchString}
          />
          {Object.keys(metricsToDisplay).map((key) => {
            return (
              <div className="metrics-group-container" key={key}>
                <h5>{MetricTypesWithOperations[key].title}</h5>
                {metricsToDisplay[key].map((filter) => {
                  return (
                    <Row key={filter}>
                      <Col>{getFilterCheckbox(filter, handleFilterCheckboxClick)}</Col>
                    </Row>
                  );
                })}
              </div>
            );
          })}
        </Panel>
      </YBModal>
    </div>
  );
};
