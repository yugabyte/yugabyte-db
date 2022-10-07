// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import { Link } from 'react-router';
import { Dropdown, MenuItem, FormControl } from 'react-bootstrap';
import momentLocalizer from 'react-widgets-moment';
import { withRouter, browserHistory } from 'react-router';
import moment from 'moment';
import { toast } from 'react-toastify';
import _ from 'lodash';

import {
  MetricConsts,
  MetricMeasure,
  MetricOrigin,
  DEFAULT_OUTLIER_NUM_NODES,
  MIN_OUTLIER_NUM_NODES,
  MAX_OUTLIER_NUM_NODES
} from '../../metrics/constants';
import { YBButton, YBButtonLink } from '../../common/forms/fields';
import { YBPanelItem } from '../../panels';
import { FlexContainer, FlexGrow } from '../../common/flexbox/YBFlexBox';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { isValidObject, isNonEmptyObject } from '../../../utils/ObjectUtils';
import { MetricsComparisonModal } from '../MetricsComparisonModal/MetricsComparisonModal';
import { NodeSelector } from '../MetricsComparisonModal/NodeSelector';
import { RegionSelector } from '../MetricsComparisonModal/RegionSelector';
import { CustomDatePicker } from '../CustomDatePicker/CustomDatePicker';
import { MetricsMeasureSelector } from '../MetricsMeasureSelector/MetricsMeasureSelector';
import { OutlierSelector } from '../OutlierSelector/OutlierSelector';

import './GraphPanelHeader.scss';

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

const outlierTypes = [
  { value: 'TOP', label: 'Top' },
  { value: 'BOTTOM', label: 'Bottom' },
];

const metricMeasureTypes = [
  { value: MetricMeasure.OVERALL, },
  { value: MetricMeasure.OUTLIER },
];

const DEFAULT_FILTER_KEY = 0;
const DEFAULT_INTERVAL_KEY = 0;
const DEFAULT_METRIC_MEASURE_KEY = 0;
const DEFAULT_OUTLIER_TYPE = 0;
export const DEFAULT_GRAPH_FILTER = {
  startMoment: moment().subtract(
    filterTypes[DEFAULT_FILTER_KEY].value,
    filterTypes[DEFAULT_FILTER_KEY].type
  ),
  endMoment: moment(),
  nodePrefix: MetricConsts.ALL,
  nodeName: MetricConsts.ALL,
  currentSelectedRegion: MetricConsts.ALL,
  selectedRegionClusterUUID: null,
  selectedRegionCode: null,
  selectedZoneName: null,
  metricMeasure: MetricMeasure.OVERALL,
  outlierType: 'Top',
  outlierNumNodes: DEFAULT_OUTLIER_NUM_NODES,
  filterLabel: filterTypes[DEFAULT_FILTER_KEY].label,
  filterType: filterTypes[DEFAULT_FILTER_KEY].type,
  filterValue: filterTypes[DEFAULT_FILTER_KEY].value,
};

class GraphPanelHeader extends Component {
  constructor(props) {
    super(props);
    // Reset graph filters when user switches from Metrics view to individual Universe Metrics view
    this.props.resetMetrics();
    momentLocalizer(moment);
    const defaultFilter = filterTypes[DEFAULT_FILTER_KEY];
    let currentUniverse = MetricConsts.ALL;
    let currentUniversePrefix = MetricConsts.ALL;
    let currentRegion = MetricConsts.ALL;

    if (this.props.origin === MetricOrigin.UNIVERSE) {
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
      currentSelectedRegion: currentRegion,
      selectedRegionCode: null,
      selectedZoneName: null,
      selectedRegionClusterUUID: null,
      endMoment: moment(),
      startMoment: moment().subtract('1', 'hours'),
      nodePrefix: currentUniversePrefix,
      nodeName: MetricConsts.ALL,
      refreshInterval: intervalTypes[DEFAULT_INTERVAL_KEY].value,
      refreshIntervalLabel: intervalTypes[DEFAULT_INTERVAL_KEY].selectedLabel,
      metricMeasure: metricMeasureTypes[DEFAULT_METRIC_MEASURE_KEY].value,
      outlierType: outlierTypes[DEFAULT_OUTLIER_TYPE].value,
      outlierNumNodes: DEFAULT_OUTLIER_NUM_NODES,
      isOneNodeInSelectedZone: false,
      isOneNodeInSelectedRegion: false
    };

    if (isValidObject(currentQuery) && Object.keys(currentQuery).length > 1) {
      const filterParams = {
        nodePrefix: currentQuery.nodePrefix,
        nodeName: currentQuery.nodeName,
        filterType: currentQuery.filterType,
        filterValue: currentQuery.filterValue,
        currentSelectedRegion: currentQuery.currentSelectedRegion,
        selectedRegionCode: currentQuery.selectedRegionCode,
        selectedZoneName: currentQuery.selectedZoneName,
        selectedRegionClusterUUID: currentQuery.selectedRegionClusterUUID,
        metricMeasure: currentQuery.metricMeasure,
        outlierType: currentQuery.outlierType,
        outlierNumNodes: currentQuery.outlierNumNodes
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
    if (this.props.origin === MetricOrigin.CUSTOMER && getPromiseState(universeList).isInit()) {
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
          currentUniverse = MetricConsts.ALL;
        }
      } else {
        currentUniverse = universe.currentUniverse.data;
      }
      let currentSelectedNode = MetricConsts.ALL;
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

  universeItemChanged = (universeUUID) => {
    const {
      universe: { universeList }
    } = this.props;
    const selectedUniverseUUID = universeUUID;
    const self = this;
    const newParams = this.state;
    const matchedUniverse = universeList.data.find((u) => u.universeUUID === selectedUniverseUUID);
    if (matchedUniverse) {
      self.setState({
        currentSelectedUniverse: matchedUniverse,
        nodePrefix: matchedUniverse.universeDetails.nodePrefix,
        nodeName: MetricConsts.ALL,
        currentSelectedRegion: MetricConsts.ALL,
        selectedRegionCode: null,
        selectedZoneName: null,
        selectedRegionClusterUUID: null,
        metricMeasure: metricMeasureTypes[0].value,
        isOneNodeInSelectedZone: false,
        isOneNodeInSelectedRegion: false
      });
      newParams.nodePrefix = matchedUniverse.universeDetails.nodePrefix;
    } else {
      self.setState({
        nodeName: MetricConsts.ALL,
        nodePrefix: MetricConsts.ALL,
        currentSelectedRegion: MetricConsts.ALL,
        selectedRegionCode: null,
        selectedZoneName: null,
        selectedRegionClusterUUID: null,
        metricMeasure: metricMeasureTypes[0].value,
        isOneNodeInSelectedZone: false,
        isOneNodeInSelectedRegion: false
      });
      newParams.nodePrefix = null;
    }
    newParams.metricMeasure = metricMeasureTypes[0].value;
    newParams.selectedRegionClusterUUID = null;
    newParams.selectedRegionCode = null;
    newParams.selectedZoneName = null;
    newParams.currentSelectedRegion = MetricConsts.ALL;

    newParams.nodeName = MetricConsts.ALL;
    this.updateUrlQueryParams(newParams);
  };

  // TODO: Needs to be removed once Top K metrics is tested and integrated fully
  universeItemChangedOld = (event) => {
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

  // TODO: Needs to be removed once Top K metrics is tested and integrated fully
  nodeItemChangedOld = (event) => {
    const newParams = this.state;
    newParams.nodeName = event.target.value;
    this.setState({ nodeName: event.target.value, currentSelectedNode: event.target.value });
    this.updateUrlQueryParams(newParams);
  };

  nodeItemChanged = (nodeName, nodeItems, selectedZoneName) => {
    let nodesInSelectedZone = [];
    if (selectedZoneName) {
      nodesInSelectedZone = nodeItems.filter((nodeItem) => selectedZoneName === nodeItem.cloudInfo.az);
    }

    const newParams = _.cloneDeep(this.state);
    newParams.nodeName = nodeName;
    newParams.selectedZoneName = selectedZoneName;
    this.setState({
      nodeName: nodeName,
      selectedZoneName: selectedZoneName,
      isOneNodeInSelectedZone: false
    });

    // When number of nodes in a selected zone is 1, button focus should move to Overall
    if (nodeName && (nodeName !== MetricConsts.ALL || nodesInSelectedZone?.length === 1)) {
      this.setState({
        metricMeasure: MetricMeasure.OVERALL,
        isOneNodeInSelectedZone: true
      });
      newParams.metricMeasure = MetricMeasure.OVERALL;
    }
    this.updateUrlQueryParams(newParams);
  };

  onRegionChanged = (region, regionCode, clusterId) => {
    const newParams = _.cloneDeep(this.state);
    this.setState({
      currentSelectedRegion: region,
      selectedRegionClusterUUID: clusterId,
      selectedRegionCode: regionCode,
      nodeName: MetricConsts.ALL,
      isOneNodeInSelectedZone: false,
      isOneNodeInSelectedRegion: false,
      // Make sure zone and node dropdown resets everytime we change region and cluster dropdown
      selectedZoneName: null,
    });

    const currentSelectedUniverse = this.state.currentSelectedUniverse;
    const nodeDetailsSet = currentSelectedUniverse?.universeDetails?.nodeDetailsSet;
    // When number of nodes in a selected region or cluster is 1, button focus should move to Overall
    if (clusterId && nodeDetailsSet?.length) {
      const nodesInSelectedRegion = nodeDetailsSet.filter(
        (nodeDetail) => clusterId === nodeDetail.placementUuid);
      if (nodesInSelectedRegion.length === 1) {
        this.setState({
          metricMeasure: MetricMeasure.OVERALL,
          isOneNodeInSelectedRegion: true,
          // If there is one node in selected region, then there cannot be more than one node in the zone
          isOneNodeInSelectedZone: true,
        });
        newParams.metricMeasure = MetricMeasure.OVERALL;
      }
    }
    newParams.selectedZoneName = null;
    newParams.nodeName = MetricConsts.ALL;
    newParams.selectedRegionCode = regionCode;
    newParams.currentSelectedRegion = region;
    newParams.selectedRegionClusterUUID = clusterId;
    this.updateUrlQueryParams(newParams);
  }

  onMetricMeasureChanged = (selectedMetricMeasureValue) => {
    const newParams = _.cloneDeep(this.state);
    if (this.state.nodeName &&
      this.state.nodeName === MetricConsts.ALL) {
      this.setState({
        metricMeasure: selectedMetricMeasureValue
      });
      newParams.metricMeasure = selectedMetricMeasureValue;
      this.updateUrlQueryParams(newParams);
    }
  }

  onOutlierTypeChanged = (selectedOutlierType) => {
    const newParams = _.cloneDeep(this.state);
    this.setState({
      outlierType: selectedOutlierType
    });
    newParams.outlierType = selectedOutlierType;
    this.updateUrlQueryParams(newParams);
  }

  setNumNodeValue = (outlierNumNodes) => {
    const newParams = _.cloneDeep(this.state);
    if (typeof outlierNumNodes === 'number'
      && outlierNumNodes >= MIN_OUTLIER_NUM_NODES && outlierNumNodes <= MAX_OUTLIER_NUM_NODES
    ) {
      this.setState({
        outlierNumNodes
      });
      newParams.outlierNumNodes = outlierNumNodes;
    } else {
      this.setState({
        outlierNumNodes: DEFAULT_OUTLIER_NUM_NODES
      });
      newParams.outlierNumNodes = DEFAULT_OUTLIER_NUM_NODES;
    }
    this.updateUrlQueryParams(newParams);
  }

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

  // This function ensures when we refresh the browser, the dropdown values will be preserved
  updateUrlQueryParams = (filterParams) => {
    const location = Object.assign({}, browserHistory.getCurrentLocation());
    const queryParams = location.query;
    const isEnabledTopKMetrics = this.props.enableTopKMetrics;

    // TODO: Needs to be removed once Top K metrics is tested and integrated fully
    if (isEnabledTopKMetrics) {
      queryParams.currentSelectedRegion = filterParams.currentSelectedRegion;
      queryParams.selectedZoneName = filterParams.selectedZoneName;
      queryParams.selectedRegionClusterUUID = filterParams.selectedRegionClusterUUID;
      queryParams.metricMeasure = filterParams.metricMeasure;
      queryParams.outlierType = filterParams.outlierType;
      queryParams.outlierNumNodes = filterParams.outlierNumNodes;
    }

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
      enableNodeComparisonModal,
      enableTopNodes,
      enableTopKMetrics
    } = this.props;
    const {
      filterType,
      refreshIntervalLabel,
      startMoment,
      endMoment,
      currentSelectedUniverse,
      currentSelectedRegion,
      selectedRegionClusterUUID,
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
    if (origin === MetricOrigin.CUSTOMER) {
      if (enableTopKMetrics) {
        universePicker = <UniversePicker
          {...this.props}
          universeItemChanged={this.universeItemChanged}
          selectedUniverse={self.state.currentSelectedUniverse}
        />
      } else {
        // TODO: Needs to be removed once Top K metrics is tested and integrated fully
        universePicker = (
          <UniversePickerOld
            {...this.props}
            universeItemChanged={this.universeItemChangedOld}
            selectedUniverse={self.state.currentSelectedUniverse}
          />
        );
      }
    }

    // TODO: Need to fix handling of query params on Metrics tab
    const liveQueriesLink =
      this.state.currentSelectedUniverse &&
      this.state.nodeName !== MetricConsts.ALL && this.state.nodeName !== MetricConsts.TOP &&
      `/universes/${this.state.currentSelectedUniverse.universeUUID}/queries?nodeName=${this.state.nodeName}`;
    return (
      <div className="graph-panel-header">
        <YBPanelItem
          className="graph-panel"
          header={
            <div>
              {this.props.origin === MetricOrigin.CUSTOMER ? (
                <h2 className="task-list-header content-title">Metrics</h2>
              ) : (
                ''
              )}
              <FlexContainer>
                <FlexGrow power={1}>
                  <div className="filter-container">
                    {universePicker}
                    {enableTopKMetrics && this.props.origin !== MetricOrigin.TABLE &&
                      <RegionSelector
                        selectedUniverse={this.state.currentSelectedUniverse}
                        onRegionChanged={this.onRegionChanged}
                        currentSelectedRegion={currentSelectedRegion}
                        selectedRegionClusterUUID={selectedRegionClusterUUID}
                      />}
                    {this.props.origin !== MetricOrigin.TABLE &&
                      <NodeSelector
                        {...this.props}
                        nodeItemChanged={this.nodeItemChanged}
                        nodeItemChangedOld={this.nodeItemChangedOld}
                        selectedUniverse={this.state.currentSelectedUniverse}
                        selectedNode={this.state.nodeName}
                        topNodesSelection={enableTopNodes}
                        selectedRegionClusterUUID={selectedRegionClusterUUID}
                        selectedZoneName={this.state.selectedZoneName}
                        enableTopKMetrics={enableTopKMetrics}
                      />}
                    {liveQueriesLink && !universePaused && !enableTopKMetrics &&(
                      <Link to={liveQueriesLink} style={{ marginLeft: '15px' }}>
                        <i className="fa fa-search" /> See Queries
                      </Link>
                    )}
                    {liveQueriesLink && !universePaused && enableTopKMetrics && (
                      <span className="live-queries">
                        <Link to={liveQueriesLink}>
                          <span className="live-queries-label">See Queries</span>
                        </Link>
                      </span>
                    )}
                    {enableNodeComparisonModal && (
                      <YBButton
                        btnIcon={'fa fa-refresh'}
                        btnClass="btn btn-default"
                        btnText="Compare"
                        disabled={filterType === 'custom' || currentSelectedUniverse === MetricConsts.ALL}
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
                          <MenuItem onClick={() => {
                            self.props.getGrafanaJson()
                              .then((response) => {
                                return new Blob([JSON.stringify(response.data, null, 2)], {
                                  type: 'application/json'
                                });
                              })
                              .catch((error) => {
                                toast.error("Error in downloading Grafana JSON: " + error.message);
                                return null;
                              })
                              .then((blob) => {
                                if (blob != null) {
                                  const url = window.URL.createObjectURL(blob);
                                  const a = document.createElement('a');
                                  a.style.display = 'none';
                                  a.href = url;
                                  a.download = 'Grafana_Dashboard.json';
                                  document.body.appendChild(a);
                                  a.click();
                                  window.URL.revokeObjectURL(url);
                                  a.remove();
                                }
                              })
                          }}>
                            {'Download Grafana JSON'}
                          </MenuItem>
                        </Dropdown.Menu>
                      </Dropdown>
                    </div>
                  </form>
                </FlexGrow>
              </FlexContainer>
              <FlexContainer>
                <FlexGrow power={1}>
                  {enableTopKMetrics && this.state.currentSelectedUniverse !== MetricConsts.ALL &&
                    this.props.origin !== MetricOrigin.TABLE &&
                    <MetricsMeasureSelector
                      metricMeasureTypes={metricMeasureTypes}
                      selectedMetricMeasureValue={this.state.metricMeasure}
                      onMetricMeasureChanged={this.onMetricMeasureChanged}
                      selectedNode={this.state.nodeName}
                      isOneNodeInSelectedZone={this.state.isOneNodeInSelectedZone}
                      isOneNodeInSelectedRegion={this.state.isOneNodeInSelectedRegion}
                    />
                  }
                </FlexGrow>
                <FlexGrow>
                  <div className=" date-picker pull-right">
                    {datePicker}
                    <Dropdown id="graphFilterDropdown" pullRight>
                      <Dropdown.Toggle>
                        <i className="fa fa-clock-o"></i>&nbsp;
                        {this.state.filterLabel}
                      </Dropdown.Toggle>
                      <Dropdown.Menu>{menuItems}</Dropdown.Menu>
                    </Dropdown>
                  </div>
                </FlexGrow>
              </FlexContainer>
              <FlexContainer>
                <FlexGrow power={1}>
                  {enableTopKMetrics && this.state.metricMeasure === MetricMeasure.OUTLIER &&
                    <OutlierSelector
                      outlierTypes={outlierTypes}
                      selectedOutlierType={this.state.outlierType}
                      onOutlierTypeChanged={this.onOutlierTypeChanged}
                      setNumNodeValue={this.setNumNodeValue}
                      defaultOutlierNumNodes={this.state.outlierNumNodes}
                    />}
                </FlexGrow>
              </FlexContainer>
              {enableNodeComparisonModal ? (
                <MetricsComparisonModal
                  visible={showModal && visibleModal === 'metricsComparisonModal'}
                  onHide={closeModal}
                  selectedUniverse={this.state.currentSelectedUniverse}
                  origin={origin}
                  selectedRegionClusterUUID={selectedRegionClusterUUID}
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
      </div>

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
          <MenuItem
            onSelect={universeItemChanged}
            key={idx}
            // Added this line due to the issue that dropdown does not close
            // when a menu item is selected
            onClick={() => { document.body.click() }}
            eventKey={item.universeUUID}
            active={item.universeUUID === selectedUniverse.universeUUID}
          >
            {item.name}
          </MenuItem>
        );
      });

    const defaultMenuItem = (
      <Fragment>
        <MenuItem
          onSelect={universeItemChanged}
          key={-1}
          // Added this line due to the issue that dropdown does not close
          // when a menu item is selected
          active={selectedUniverse === MetricConsts.ALL}
          onClick={() => { document.body.click() }}
          eventKey={MetricConsts.ALL}
        >
          {"All universes"}
        </MenuItem>
      </Fragment>
    );

    let currentUniverseValue = MetricConsts.ALL;
    if (!_.isString(selectedUniverse)) {
      currentUniverseValue = selectedUniverse.name;
    }

    universeItems.splice(0, 0, defaultMenuItem);
    return (
      <div className="universe-picker-container">
        <Dropdown
          id="universe-filter-dropdown"
        >
          <Dropdown.Toggle className="dropdown-toggle-button">
            <span className="default-universe-value">
              {currentUniverseValue === MetricConsts.ALL ? universeItems[0] : currentUniverseValue}
            </span>
          </Dropdown.Toggle>
          <Dropdown.Menu>
            <span className={"universe-name"}>{universeItems}</span>
          </Dropdown.Menu>
        </Dropdown>
      </div>
    );
  }
}

// TODO: Needs to be removed once Top K metrics is tested and integrated fully
class UniversePickerOld extends Component {
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
