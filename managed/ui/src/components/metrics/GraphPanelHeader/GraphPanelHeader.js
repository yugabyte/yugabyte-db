// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';
import { Dropdown, MenuItem, FormControl } from 'react-bootstrap';
import momentLocalizer from 'react-widgets-moment';
import { withRouter, browserHistory } from 'react-router';
import moment from 'moment';

import _ from 'lodash';

import { YBButton, YBButtonLink } from '../../common/forms/fields';
import { YBPanelItem } from '../../panels';
import { FlexContainer, FlexGrow } from '../../common/flexbox/YBFlexBox';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { isValidObject, isNonEmptyObject } from '../../../utils/ObjectUtils';
import './GraphPanelHeader.scss';
import { MetricsComparisonModal } from '../MetricsComparisonModal/MetricsComparisonModal';
import { NodeSelector } from '../MetricsComparisonModal/NodeSelector';
import { CustomDatePicker } from '../CustomDatePicker/CustomDatePicker';

require('react-widgets/dist/css/react-widgets.css');

// We can define different filter types here, the type parameter should be
// valid type that moment supports except for custom and divider.
// if the filter type has a divider, we would just add a divider in the dropdown
// and custom filter would show custom date picker
const filterTypes = [
  { label: 'Last 1 hr', type: 'hours', value: '1' },
  { label: 'Last 6 hrs', type: 'hours', value: '6' },
  { label: 'Last 12 hrs', type: 'hours', value: '12' },
  { label: 'Last 24 hrs', type: 'hours', value: '24' },
  { label: 'Last 7 days', type: 'days', value: '7' },
  { type: 'divider' },
  { label: 'Custom', type: 'custom' }
];

const intervalTypes = [
  { label: 'Off', selectedLabel: 'Off', value: 'off' },
  { label: 'Every 1 minute', selectedLabel: '1 minute', value: 60000 },
  { label: 'Every 2 minutes', selectedLabel: '2 minute', value: 120000 }
];

const DEFAULT_FILTER_KEY = 0;
const DEFAULT_INTERVAL_KEY = 0;
export const DEFAULT_GRAPH_FILTER = {
  startMoment: moment().subtract(
    filterTypes[DEFAULT_FILTER_KEY].value,
    filterTypes[DEFAULT_FILTER_KEY].type
  ),
  endMoment: moment(),
  nodePrefix: 'all',
  nodeName: 'all',
  filterLabel: filterTypes[DEFAULT_FILTER_KEY].label,
  filterType: filterTypes[DEFAULT_FILTER_KEY].type,
  filterValue: filterTypes[DEFAULT_FILTER_KEY].value
};

class GraphPanelHeader extends Component {
  constructor(props) {
    super(props);
    momentLocalizer(moment);
    const defaultFilter = filterTypes[DEFAULT_FILTER_KEY];
    let currentUniverse = 'all';
    let currentUniversePrefix = 'all';

    if (this.props.origin === 'universe') {
      currentUniverse = this.props.universe.currentUniverse.data;
      currentUniversePrefix = currentUniverse.universeDetails.nodePrefix;
    }

    const location = browserHistory.getCurrentLocation();
    const currentQuery = location.query;
    // Remove subtab from query param
    delete currentQuery.subtab;

    const defaultFilters = {
      showDatePicker: false,
      filterLabel: defaultFilter.label,
      filterType: defaultFilter.type,
      filterValue: defaultFilter.value,
      currentSelectedUniverse: currentUniverse,
      endMoment: moment(),
      startMoment: moment().subtract('1', 'hours'),
      nodePrefix: currentUniversePrefix,
      nodeName: 'all',
      refreshInterval: intervalTypes[DEFAULT_INTERVAL_KEY].value,
      refreshIntervalLabel: intervalTypes[DEFAULT_INTERVAL_KEY].selectedLabel
    };

    if (isValidObject(currentQuery) && Object.keys(currentQuery).length > 1) {
      const filterParams = {
        nodePrefix: currentQuery.nodePrefix,
        nodeName: currentQuery.nodeName,
        filterType: currentQuery.filterType,
        filterValue: currentQuery.filterValue
      };
      if (currentQuery.filterType === 'custom') {
        filterParams.startMoment = moment.unix(currentQuery.startDate);
        filterParams.endMoment = moment.unix(currentQuery.endDate);
        filterParams.filterValue = '';
        filterParams.filterLabel = 'Custom';
      } else {
        const currentFilterItem = filterTypes.find(
          (filterType) =>
            filterType.type === currentQuery.filterType &&
            filterType.value === currentQuery.filterValue
        );
        filterParams.filterLabel = currentFilterItem.label;
        filterParams.endMoment = moment();
        filterParams.startMoment = moment().subtract(
          currentFilterItem.value,
          currentFilterItem.type
        );
      }

      this.state = {
        ...defaultFilters,
        ...filterParams
      };
      props.changeGraphQueryFilters(filterParams);
    } else {
      this.state = defaultFilters;
    }
  }

  componentDidMount() {
    const {
      universe: { universeList }
    } = this.props;
    if (this.props.origin === 'customer' && getPromiseState(universeList).isInit()) {
      this.props.fetchUniverseList();
    }
  }

  componentDidUpdate(prevProps) {
    const {
      location,
      universe,
      universe: { universeList }
    } = this.props;
    if (
      prevProps.location !== this.props.location ||
      (getPromiseState(universeList).isSuccess() &&
        getPromiseState(prevProps.universe.universeList).isLoading())
    ) {
      let nodePrefix = this.state.nodePrefix;
      if (location.query.nodePrefix) {
        nodePrefix = location.query.nodePrefix;
      }
      let currentUniverse;
      if (
        getPromiseState(universe.currentUniverse).isEmpty() ||
        getPromiseState(universe.currentUniverse).isInit()
      ) {
        currentUniverse = universeList.data.find(function (item) {
          return item.universeDetails.nodePrefix === nodePrefix;
        });
        if (!isNonEmptyObject(currentUniverse)) {
          currentUniverse = 'all';
        }
      } else {
        currentUniverse = universe.currentUniverse.data;
      }
      let currentSelectedNode = 'all';
      if (isValidObject(location.query.nodeName)) {
        currentSelectedNode = location.query.nodeName;
      }
      this.setState({
        currentSelectedNode,
        currentSelectedUniverse: currentUniverse
      });
    }
  }

  shouldComponentUpdate(nextProps, nextState) {
    return (
      !_.isEqual(nextState, this.state) ||
      !_.isEqual(nextProps.universe, this.props.universe) ||
      this.props.prometheusQueryEnabled !== nextProps.prometheusQueryEnabled ||
      this.props.visibleModal !== nextProps.visibleModal
    );
  }

  componentWillUnmount() {
    clearInterval(this.refreshInterval);
  }

  submitGraphFilters = (type, val) => {
    const queryObject = this.state.filterParams;
    queryObject[type] = val;
    this.updateUrlQueryParams(queryObject);
  };

  refreshGraphQuery = () => {
    const newParams = this.state;
    if (newParams.filterLabel !== 'Custom') {
      newParams.startMoment = moment().subtract(newParams.filterValue, newParams.filterType);
      newParams.endMoment = moment();
      this.props.changeGraphQueryFilters(newParams);
    }
  };

  handleFilterChange = (eventKey, event) => {
    const filterInfo = filterTypes[eventKey] || filterTypes[DEFAULT_FILTER_KEY];
    const newParams = _.cloneDeep(this.state);
    newParams.filterLabel = filterInfo.label;
    newParams.filterType = filterInfo.type;
    newParams.filterValue = filterInfo.value;
    let refreshIntervalParams = {};

    if (filterInfo.type === 'custom') {
      clearInterval(this.refreshInterval);
      refreshIntervalParams = {
        refreshInterval: intervalTypes[DEFAULT_INTERVAL_KEY].value,
        refreshIntervalLabel: intervalTypes[DEFAULT_INTERVAL_KEY].selectedLabel
      };
    }
    this.setState({
      ...refreshIntervalParams,
      filterLabel: filterInfo.label,
      filterType: filterInfo.type,
      filterValue: filterInfo.value
    });

    if (event.target.getAttribute('data-filter-type') !== 'custom') {
      const endMoment = moment();
      const startMoment = moment().subtract(filterInfo.value, filterInfo.type);
      newParams.startMoment = startMoment;
      newParams.endMoment = endMoment;
      this.setState({ startMoment: startMoment, endMoment: endMoment });
      this.updateUrlQueryParams(newParams);
    }
  };

  // Turns off auto-refresh if the interval type is 'off', otherwise resets the interval
  handleIntervalChange = (eventKey) => {
    const intervalInfo = intervalTypes[eventKey] || intervalTypes[DEFAULT_FILTER_KEY];
    clearInterval(this.refreshInterval);
    if (intervalInfo.value !== 'off') {
      this.refreshInterval = setInterval(() => this.refreshGraphQuery(), intervalInfo.value);
    }
    this.setState({
      refreshInterval: intervalInfo.value,
      refreshIntervalLabel: intervalInfo.selectedLabel
    });
  };

  universeItemChanged = (event) => {
    const {
      universe: { universeList }
    } = this.props;
    const selectedUniverseUUID = event.target.value;
    const self = this;
    const newParams = this.state;
    const matchedUniverse = universeList.data.find((u) => u.universeUUID === selectedUniverseUUID);
    if (matchedUniverse) {
      self.setState({
        currentSelectedUniverse: matchedUniverse,
        nodePrefix: matchedUniverse.universeDetails.nodePrefix,
        nodeName: 'all'
      });
      newParams.nodePrefix = matchedUniverse.universeDetails.nodePrefix;
    } else {
      self.setState({ nodeName: 'all', nodePrefix: 'all' });
      newParams.nodePrefix = null;
    }
    newParams.nodeName = 'all';
    this.updateUrlQueryParams(newParams);
  };

  nodeItemChanged = (event) => {
    const newParams = this.state;
    newParams.nodeName = event.target.value;
    this.setState({ nodeName: event.target.value, currentSelectedNode: event.target.value });
    this.updateUrlQueryParams(newParams);
  };

  handleStartDateChange = (dateStr) => {
    this.setState({ startMoment: moment(dateStr) });
  };

  handleEndDateChange = (dateStr) => {
    this.setState({ endMoment: moment(dateStr) });
  };

  applyCustomFilter = () => {
    this.updateGraphQueryParams(this.state.startMoment, this.state.endMoment);
  };

  updateGraphQueryParams = (startMoment, endMoment) => {
    const newFilterParams = this.state;
    if (isValidObject(startMoment) && isValidObject(endMoment)) {
      newFilterParams.startMoment = startMoment;
      newFilterParams.endMoment = endMoment;
      this.props.changeGraphQueryFilters(this.state);
      this.updateUrlQueryParams(newFilterParams);
    }
  };

  updateUrlQueryParams = (filterParams) => {
    const location = Object.assign({}, browserHistory.getCurrentLocation());
    const queryParams = location.query;
    queryParams.nodePrefix = filterParams.nodePrefix;
    queryParams.nodeName = filterParams.nodeName;
    queryParams.filterType = filterParams.filterType;
    queryParams.filterValue = filterParams.filterValue;
    if (queryParams.filterType === 'custom') {
      queryParams.startDate = filterParams.startMoment.unix();
      queryParams.endDate = filterParams.endMoment.unix();
    }
    Object.assign(location.query, queryParams);
    browserHistory.push(location);
    this.props.changeGraphQueryFilters(filterParams);
  };

  render() {
    const {
      origin,
      universe: { currentUniverse },
      prometheusQueryEnabled,
      customer: { currentUser },
      showModal,
      closeModal,
      visibleModal,
      enableNodeComparisonModal
    } = this.props;
    const {
      filterType,
      refreshIntervalLabel,
      startMoment,
      endMoment,
      currentSelectedUniverse
    } = this.state;
    const universePaused = currentUniverse?.data?.universeDetails?.universePaused;
    let datePicker = null;
    if (this.state.filterLabel === 'Custom') {
      datePicker = (
        <CustomDatePicker
          startMoment={startMoment}
          endMoment={endMoment}
          handleTimeframeChange={this.applyCustomFilter}
          setStartMoment={this.handleStartDateChange}
          setEndMoment={this.handleEndDateChange}
        />
      );
    }

    const self = this;
    const menuItems = filterTypes.map((filter, idx) => {
      const key = 'graph-filter-' + idx;
      if (filter.type === 'divider') {
        return <MenuItem divider key={key} />;
      }

      return (
        <MenuItem
          onSelect={self.handleFilterChange}
          data-filter-type={filter.type}
          key={key}
          eventKey={idx}
          active={filter.label === self.state.filterLabel}
        >
          {filter.label}
        </MenuItem>
      );
    });

    const intervalMenuItems = intervalTypes.map((interval, idx) => {
      const key = 'graph-interval-' + idx;
      return (
        <MenuItem
          onSelect={self.handleIntervalChange}
          key={key}
          eventKey={idx}
          active={interval.value === self.state.refreshInterval}
        >
          {interval.label}
        </MenuItem>
      );
    });

    let universePicker = <span />;
    if (origin === 'customer') {
      universePicker = (
        <UniversePicker
          {...this.props}
          universeItemChanged={this.universeItemChanged}
          selectedUniverse={self.state.currentSelectedUniverse}
        />
      );
    }
    // TODO: Need to fix handling of query params on Metrics tab
    const liveQueriesLink =
      this.state.currentSelectedUniverse &&
      this.state.nodeName !== 'all' &&
      `/universes/${this.state.currentSelectedUniverse.universeUUID}/queries?nodeName=${this.state.nodeName}`;

    return (
      <YBPanelItem
        className="graph-panel"
        header={
          <div>
            {this.props.origin === 'customer' ? (
              <h2 className="task-list-header content-title">Metrics</h2>
            ) : (
              ''
            )}
            <FlexContainer>
              <FlexGrow power={1}>
                <div className="filter-container">
                  {universePicker}
                  <NodeSelector
                    {...this.props}
                    nodeItemChanged={this.nodeItemChanged}
                    selectedUniverse={this.state.currentSelectedUniverse}
                    selectedNode={this.state.currentSelectedNode}
                  />
                  {liveQueriesLink && !universePaused && (
                    <Link to={liveQueriesLink} style={{ marginLeft: '15px' }}>
                      <i className="fa fa-search" /> See Queries
                    </Link>
                  )}
                  {enableNodeComparisonModal && (
                    <YBButton
                      btnIcon={'fa fa-refresh'}
                      btnClass="btn btn-default"
                      btnText="Compare"
                      disabled={filterType === 'custom' || currentSelectedUniverse === 'all'}
                      onClick={() => showModal('metricsComparisonModal')}
                    />
                  )}
                </div>
              </FlexGrow>
              <FlexGrow>
                <form name="GraphPanelFilterForm">
                  <div id="reportrange" className="pull-right">
                    <div className="timezone">
                      Timezone:{' '}
                      {currentUser.data.timezone
                        ? moment.tz(currentUser.data.timezone).format('[UTC]ZZ')
                        : moment().format('[UTC]ZZ')}
                    </div>
                    <div className="graph-interval-container">
                      <Dropdown
                        id="graph-interval-dropdown"
                        disabled={filterType === 'custom'}
                        pullRight
                      >
                        <Dropdown.Toggle className="dropdown-toggle-button">
                          Auto Refresh:&nbsp;
                          <span className="chip" key={`interval-token`}>
                            <span className="value">{refreshIntervalLabel}</span>
                          </span>
                        </Dropdown.Toggle>
                        <Dropdown.Menu>{intervalMenuItems}</Dropdown.Menu>
                      </Dropdown>
                      <YBButtonLink
                        btnIcon={'fa fa-refresh'}
                        btnClass="btn btn-default refresh-btn"
                        disabled={filterType === 'custom'}
                        onClick={this.refreshGraphQuery}
                      />
                    </div>
                    {datePicker}
                    <Dropdown id="graphFilterDropdown" className="graph-filter-dropdown" pullRight>
                      <Dropdown.Toggle>
                        <i className="fa fa-clock-o"></i>&nbsp;
                        {this.state.filterLabel}
                      </Dropdown.Toggle>
                      <Dropdown.Menu>{menuItems}</Dropdown.Menu>
                    </Dropdown>
                    <Dropdown
                      id="graphSettingDropdown"
                      className="graph-setting-dropdown"
                      pullRight
                    >
                      <Dropdown.Toggle noCaret>
                        <i className="graph-settings-icon fa fa-cog"></i>
                      </Dropdown.Toggle>
                      <Dropdown.Menu>
                        <MenuItem className="dropdown-header" header>
                          VIEW OPTIONS
                        </MenuItem>
                        <MenuItem divider />
                        <MenuItem onSelect={self.props.togglePrometheusQuery}>
                          {prometheusQueryEnabled
                            ? 'Disable Prometheus query'
                            : 'Enable Prometheus query'}
                        </MenuItem>
                      </Dropdown.Menu>
                    </Dropdown>
                  </div>
                </form>
              </FlexGrow>
            </FlexContainer>

            {enableNodeComparisonModal ? (
              <MetricsComparisonModal
                visible={showModal && visibleModal === 'metricsComparisonModal'}
                onHide={closeModal}
                selectedUniverse={this.state.currentSelectedUniverse}
                origin={origin}
              />
            ) : (
              ''
            )}
          </div>
        }
        /* React.cloneELement for passing state down to child components in HOC */
        body={React.cloneElement(this.props.children, {
          selectedUniverse: this.state.currentSelectedUniverse
        })}
        noBackground
      />
    );
  }
}

export default withRouter(GraphPanelHeader);

class UniversePicker extends Component {
  render() {
    const {
      universeItemChanged,
      universe: { universeList },
      selectedUniverse
    } = this.props;

    const universeItems = universeList.data
      .sort((a, b) => a.name.toLowerCase() > b.name.toLowerCase())
      .map(function (item, idx) {
        return (
          <option key={idx} value={item.universeUUID} name={item.name}>
            {item.name}
          </option>
        );
      });

    const universeOptionArray = [
      <option key={-1} value="all">
        All
      </option>
    ].concat(universeItems);
    let currentUniverseValue = 'all';

    if (!_.isString(selectedUniverse)) {
      currentUniverseValue = selectedUniverse.universeUUID;
    }
    return (
      <div className="universe-picker">
        Universe:
        <FormControl
          componentClass="select"
          placeholder="select"
          onChange={universeItemChanged}
          value={currentUniverseValue}
        >
          {universeOptionArray}
        </FormControl>
      </div>
    );
  }
}
