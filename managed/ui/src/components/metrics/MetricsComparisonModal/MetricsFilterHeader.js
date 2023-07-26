import { useContext, useState } from 'react';
import { Dropdown, MenuItem } from 'react-bootstrap';
import moment from 'moment';

import { YBButton, YBButtonLink } from '../../common/forms/fields';
import { FlexContainer, FlexGrow } from '../../common/flexbox/YBFlexBox';
import './MetricsComparisonModal.scss';
import '../GraphPanelHeader/GraphPanelHeader.scss';
import { useSelector } from 'react-redux';
import {
  DEFAULT_FILTER_KEY,
  FilterContext,
  filterTypes,
  intervalTypes
} from './ComparisonFilterContextProvider';
import { MetricsSelectorModal } from './MetricsSelectorModal';
import { CustomDatePicker } from '../CustomDatePicker/CustomDatePicker';

import 'react-widgets/dist/css/react-widgets.css';

const TIMESTAMP_FORMAT = '[UTC]ZZ';

export const MetricsFilterHeader = ({ origin, selectedUniverse }) => {
  const currentUser = useSelector((reduxState) => reduxState.customer.currentUser);

  const [state, dispatch] = useContext(FilterContext);
  const { filters, refreshFilters, visibleMetricsSelectorModal } = state;

  const [customStartMoment, setCustomStartMoment] = useState(filters.startMoment);
  const [customEndMoment, setCustomEndMoment] = useState(filters.endMoment);

  const handleHideModal = () => {
    dispatch({
      type: 'HIDE_METRICS_MODAL'
    });
  };

  const handleShowModal = () => {
    dispatch({
      type: 'SHOW_METRICS_MODAL'
    });
  };

  const handleFilterChange = (eventKey, event, dataFilterType) => {
    const filterInfo = filterTypes[eventKey] || filterTypes[DEFAULT_FILTER_KEY];
    if (filterInfo.type === 'custom') {
      dispatch({
        type: 'RESET_REFRESH_INTERVAL'
      });
    }

    if (dataFilterType !== 'custom') {
      filterInfo.startMoment = moment().subtract(filterInfo.value, filterInfo.type);
      filterInfo.endMoment = moment();
    }
    dispatch({
      type: 'CHANGE_GRAPH_FILTER',
      payload: { ...filterInfo }
    });
  };

  const refreshModalGraphQuery = () => {
    const newFilter = {
      startMoment: moment().subtract(filters.value, filters.type),
      endMoment: moment()
    };
    dispatch({
      type: 'CHANGE_GRAPH_FILTER',
      payload: { ...newFilter }
    });
  };

  const handleTimeframeChange = () => {
    dispatch({
      type: 'CHANGE_GRAPH_FILTER',
      payload: { startMoment: customStartMoment, endMoment: customEndMoment }
    });
  };

  const handleIntervalChange = (eventKey) => {
    const intervalInfo = intervalTypes[eventKey] || intervalTypes[DEFAULT_FILTER_KEY];
    dispatch({
      type: 'CHANGE_REFRESH_INTERVAL',
      payload: {
        refreshInterval: intervalInfo.value,
        refreshIntervalLabel: intervalInfo.selectedLabel
      }
    });
  };

  const datePicker =
    state.filters.label === 'Custom' ? (
      <CustomDatePicker
        startMoment={filters.startMoment}
        endMoment={filters.endMoment}
        setStartMoment={setCustomStartMoment}
        setEndMoment={setCustomEndMoment}
        handleTimeframeChange={handleTimeframeChange}
      />
    ) : null;

  const menuItems = filterTypes.map((filterItem, idx) => {
    const key = 'graph-filter-' + idx;
    if (filterItem.type === 'divider') {
      return <MenuItem divider key={key} />;
    }

    return (
      <MenuItem
        onSelect={(eventKey, event) => handleFilterChange(eventKey, event, filterItem.type)}
        key={key}
        eventKey={idx}
        active={filterItem.label === filters.label}
      >
        {filterItem.label}
      </MenuItem>
    );
  });

  const intervalMenuItems = intervalTypes.map((interval, idx) => {
    const key = 'graph-interval-' + idx;
    return (
      <MenuItem
        onSelect={handleIntervalChange}
        key={key}
        eventKey={idx}
        active={interval.value === refreshFilters.refreshInterval}
      >
        {interval.label}
      </MenuItem>
    );
  });

  return (
    <>
      <FlexContainer className="metrics-filter-header">
        <FlexGrow>
          <div className="comparison-modal-header-title">Compare Performance</div>
          <div id="reportrange" className="pull-right">
            <div className="metrics-filter-item timezone">
              Timezone:{' '}
              {currentUser.data.timezone
                ? moment.tz(currentUser.data.timezone).format(TIMESTAMP_FORMAT)
                : moment().format(TIMESTAMP_FORMAT)}
            </div>
            <div className="metrics-filter-item graph-interval-container">
              <Dropdown id="graph-interval-dropdown" disabled={filters.type === 'custom'} pullRight>
                <Dropdown.Toggle className="dropdown-toggle-button">
                  Auto Refresh:&nbsp;
                  <span className="chip" key={`interval-token`}>
                    <span className="value"> {refreshFilters.refreshIntervalLabel}</span>
                  </span>
                </Dropdown.Toggle>
                <Dropdown.Menu>{intervalMenuItems}</Dropdown.Menu>
              </Dropdown>
              <YBButtonLink
                btnIcon={'fa fa-refresh'}
                btnClass="btn btn-default refresh-btn"
                disabled={filters.type === 'custom'}
                onClick={refreshModalGraphQuery}
              />
            </div>
            {datePicker}
            <Dropdown
              id="graphFilterDropdown"
              className="metrics-filter-item graph-filter-dropdown"
              pullRight
            >
              <Dropdown.Toggle>
                <i className="fa fa-clock-o"></i>&nbsp;
                {filters.label}
              </Dropdown.Toggle>
              <Dropdown.Menu>{menuItems}</Dropdown.Menu>
            </Dropdown>
            <YBButton
              btnText={'Add Metrics to Compare'}
              btnClass={'btn btn-orange metrics-filter-item'}
              onClick={handleShowModal}
            />
          </div>
        </FlexGrow>
      </FlexContainer>
      <MetricsSelectorModal
        visible={visibleMetricsSelectorModal}
        onHide={handleHideModal}
        origin={origin}
        selectedUniverse={selectedUniverse}
      />
    </>
  );
};
