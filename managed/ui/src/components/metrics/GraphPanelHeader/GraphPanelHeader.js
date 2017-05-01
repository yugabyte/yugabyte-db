// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Dropdown, MenuItem, Row, Col, Grid , FormControl} from 'react-bootstrap';
import { DateTimePicker } from 'react-widgets';
import { YBButton } from '../../common/forms/fields';
require('react-widgets/dist/css/react-widgets.css');
var moment = require('moment');
import { isValidObject, isValidArray } from '../../../utils/ObjectUtils';
var momentLocalizer = require('react-widgets/lib/localizers/moment');
import './GraphPanelHeader.scss';
import { withRouter, browserHistory } from 'react-router';
import _ from 'lodash';

// We can define different filter types here, the type parameter should be
// valid type that moment supports except for custom and divider.
// if the filter type has a divider, we would just add a divider in the dropdown
// and custom filter would show custom date picker
const filterTypes = [
  {label: "Last 1 hr", type: "hours", value: "1"},
  {label: "Last 6 hrs", type: "hours", value: "6"},
  {label: "Last 12 hrs", type: "hours", value: "12"},
  {label: "Last 24 hrs", type: "hours", value: "24"},
  {label: "Last 7 days", type: "days", value: "7"},
  {type: "divider"},
  {label: "Custom", type: "custom"}
]

const DEFAULT_FILTER_KEY = 0
export const DEFAULT_GRAPH_FILTER = {
  startMoment: moment().subtract(
    filterTypes[DEFAULT_FILTER_KEY].value,
    filterTypes[DEFAULT_FILTER_KEY].type),
  endMoment: moment(),
  nodePrefix: "all",
  nodeName: "all",
  filterLabel: filterTypes[DEFAULT_FILTER_KEY].label,
  filterType: filterTypes[DEFAULT_FILTER_KEY].type,
  filterValue: filterTypes[DEFAULT_FILTER_KEY].value
}


class GraphPanelHeader extends Component {
  constructor(props) {
    super(props);
    momentLocalizer(moment);
    this.handleFilterChange = this.handleFilterChange.bind(this);
    this.handleStartDateChange = this.handleStartDateChange.bind(this);
    this.handleEndDateChange = this.handleEndDateChange.bind(this);
    this.applyCustomFilter = this.applyCustomFilter.bind(this);
    this.updateGraphQueryParams = this.updateGraphQueryParams.bind(this);
    var defaultFilter = filterTypes[DEFAULT_FILTER_KEY];
    this.universeItemChanged = this.universeItemChanged.bind(this);
    this.nodeItemChanged = this.nodeItemChanged.bind(this);
    this.submitGraphFilters = this.submitGraphFilters.bind(this);
    this.refreshGraphQuery = this.refreshGraphQuery.bind(this);
    var currentUniverse = "all";
    var currentUniversePrefix  = "all";
    if (this.props.origin === "universe") {
      currentUniverse = this.props.universe.currentUniverse;
      currentUniversePrefix = currentUniverse.universeDetails.nodePrefix;
    }
    this.updateUrlQueryParams = this.updateUrlQueryParams.bind(this);
    this.state = {
      showDatePicker: false,
      filterLabel: defaultFilter.label,
      filterType: defaultFilter.type,
      filterValue: defaultFilter.value,
      currentSelectedUniverse: currentUniverse,
      endMoment: moment(),
      startMoment: moment().subtract("1", "hours"),
      nodePrefix: currentUniversePrefix,
      nodeName: "all"
    }
  }

  componentWillMount() {
    var location = browserHistory.getCurrentLocation();
    var currentQuery = location.query;
    var currentFilters = this.state;
    if (isValidObject(currentQuery) && Object.keys(currentQuery).length > 1) {
      var filterParams = {
        nodePrefix: currentQuery.nodePrefix,
        nodeName: currentQuery.nodeName,
        filterType: currentQuery.filterType,
        filterValue: currentQuery.filterValue
      }
      if (currentQuery.filterType === "custom") {
          filterParams.startMoment = moment.unix(currentQuery.startDate);
          filterParams.endMoment = moment.unix(currentQuery.endDate);
          filterParams.filterValue  = "";
          filterParams.filterLabel = "Custom";
      } else {
        var currentFilterItem = filterTypes.find(filterType => filterType.type === currentQuery.filterType
                                                 && filterType.value === currentQuery.filterValue);
        filterParams.filterLabel = currentFilterItem.label;
        filterParams.endMoment = moment();
        filterParams.startMoment = moment().subtract(currentFilterItem.value, currentFilterItem.type);
      }
      this.setState({...filterParams});
      currentFilters = filterParams;
    }
    this.props.changeGraphQueryFilters(currentFilters);
  }

  componentWillReceiveProps(nextProps) {
    const {location, universe, universe: {universeList}} = nextProps;
    if (this.props.location !== nextProps.location || this.props.universe.universeList !== universeList) {
      var nodePrefix = this.state.nodePrefix;
      if (location.query.nodePrefix) {
        nodePrefix = location.query.nodePrefix;
      }
      if (!universe.currentUniverse) {
        var currentUniverse = universeList.find(function (item) {
          return item.universeDetails.nodePrefix === nodePrefix;
        })
        if (!isValidObject(currentUniverse) || !isValidArray(Object.keys(currentUniverse))) {
          currentUniverse = "all";
        }
      } else {
        currentUniverse = universe.currentUniverse;
      }
      var currentSelectedNode = "all";
      if (isValidObject(location.query.nodeName)) {
        currentSelectedNode = location.query.nodeName;
      }
      this.setState({currentSelectedUniverse: currentUniverse, currentSelectedNode: currentSelectedNode});
    }
  }

  submitGraphFilters(type, val) {
    var queryObject = this.state.filterParams;
    queryObject[type] = val;
    this.props.changeGraphQueryFilters(queryObject);
    this.updateUrlQueryParams(queryObject);
  }

  refreshGraphQuery() {
    var newParams = this.state;
    if (newParams.filterLabel !== "Custom") {
      newParams.startMoment = moment().subtract(newParams.filterValue, newParams.filterType);
      newParams.endMoment = moment();
      this.props.changeGraphQueryFilters(newParams);
    }
  }

  handleFilterChange(eventKey, event) {
    var filterInfo = filterTypes[eventKey] || filterTypes[DEFAULT_FILTER_KEY];
    var newParams = this.state;
    newParams.filterLabel = filterInfo.label;
    newParams.filterType = filterInfo.type;
    newParams.filterValue = filterInfo.value;
    this.setState({
      filterLabel: filterInfo.label,
      filterType: filterInfo.type,
      filterValue: filterInfo.value});

    if (event.target.getAttribute("data-filter-type") !== "custom") {
      var endMoment = moment()
      var startMoment = moment().subtract(filterInfo.value, filterInfo.type);
      newParams.startMoment = startMoment;
      newParams.endMoment = endMoment;
      this.setState({startMoment: startMoment, endMoment: endMoment});
      this.props.changeGraphQueryFilters(newParams)
      this.updateUrlQueryParams(newParams);
    }
  }

  universeItemChanged(event) {
    const {universe: {universeList}} = this.props;
    var self = this;
    var universeFound = false;
    var newParams = this.state;
    for (var counter = 0; counter < universeList.length; counter++) {
      if (universeList[counter].universeUUID === event.target.value) {
        universeFound = true;
        self.setState({currentSelectedUniverse: universeList[counter], nodePrefix: universeList[counter].universeDetails.nodePrefix,
                       nodeName: "all"});
        newParams.nodePrefix = universeList[counter].universeDetails.nodePrefix;
        break;
      }
    }
    if (!universeFound) {
      self.setState({nodeName: "all", nodePrefix: "all"})
    }
    newParams.nodeName = "all";
    this.props.changeGraphQueryFilters(newParams);
    this.updateUrlQueryParams(newParams);
  }

  nodeItemChanged(event) {
    var newParams = this.state;
    newParams.nodeName = event.target.value;
    this.props.changeGraphQueryFilters(newParams);
    this.setState({nodeName: event.target.value});
    this.updateUrlQueryParams(newParams);
  }

  handleStartDateChange(dateStr) {
    this.setState({startMoment: moment(dateStr)});
  }

  handleEndDateChange(dateStr) {
    this.setState({endMoment: moment(dateStr)})
  }

  applyCustomFilter() {
    this.updateGraphQueryParams(this.state.startMoment, this.state.endMoment)
  }

  updateGraphQueryParams(startMoment, endMoment) {
    var newFilterParams = this.state;
    if (isValidObject(startMoment) && isValidObject(endMoment)) {
      newFilterParams.startMoment = startMoment;
      newFilterParams.endMoment = endMoment;
      this.props.changeGraphQueryFilters(this.state);
      this.updateUrlQueryParams(newFilterParams);
    }
  }

  updateUrlQueryParams(filterParams) {
    const location = Object.assign({}, browserHistory.getCurrentLocation());
    var queryParams = location.query;
    queryParams.nodePrefix = filterParams.nodePrefix;
    queryParams.nodeName = filterParams.nodeName;
    queryParams.filterType = filterParams.filterType;
    queryParams.filterValue = filterParams.filterValue;
    if (queryParams.filterType === "custom") {
      queryParams.startDate = filterParams.startMoment.unix();
      queryParams.endDate = filterParams.endMoment.unix();
    }
    Object.assign(location.query, queryParams);
    browserHistory.push(location);
    this.props.changeGraphQueryFilters(filterParams);
  }

  render() {
    const { origin } = this.props;
    var datePicker = null;
    if (this.state.filterLabel === "Custom") {
      datePicker =
        <span className="graph-filter-custom" >
          <DateTimePicker
            value={this.state.startMoment.toDate()}
            onChange={this.handleStartDateChange}
            max={new Date()} />
            &nbsp;&ndash;&nbsp;
          <DateTimePicker
            value={this.state.endMoment.toDate()}
            onChange={this.handleEndDateChange}
            max={new Date()} min={this.state.startMoment.toDate()} />
            &nbsp;
          <YBButton btnIcon={"fa fa-caret-right"} onClick={this.applyCustomFilter} />
        </span>;
    }

    var self = this;
    var menuItems = filterTypes.map(function(filter, idx) {
      const key = 'graph-filter-' + idx;
      if (filter.type === "divider") {
        return <MenuItem divider key={key}/>
      }

      return (
        <MenuItem onSelect={self.handleFilterChange} data-filter-type={filter.type}
          key={key} eventKey={idx} active={filter.label === self.state.filterLabel}>
          {filter.label}
        </MenuItem>
      );
    });

    var universePicker = <span/>;
    if (origin === "customer") {
      universePicker = <UniversePicker {...this.props} universeItemChanged={this.universeItemChanged}
      selectedUniverse={self.state.currentSelectedUniverse}/>;
    }
    return (
      <Grid className="x_panel graph-panel">
        <Row className="x_title">
          <Col md={12}>
            <div className="pull-right">
              <form name="GraphPanelFilterForm">
                <div id="reportrange" className="pull-right">
                  {datePicker}
                  <Dropdown id="graph-filter-dropdown" pullRight={true} >
                    <Dropdown.Toggle>
                      <i className="fa fa-clock-o"></i>&nbsp;
                      {this.state.filterLabel}
                    </Dropdown.Toggle>
                    <Dropdown.Menu>
                      {menuItems}
                    </Dropdown.Menu>
                  </Dropdown>
                </div>
              </form>
            </div>

            <h2>Metrics</h2>
            {universePicker}
            <NodePicker {...this.props} nodeItemChanged={this.nodeItemChanged}
                        selectedUniverse={this.state.currentSelectedUniverse}
                        selectedNode={this.state.currentSelectedNode}/>
            <YBButton btnIcon="fa fa-refresh" btnClass="btn btn-default refresh-btn" onClick={this.refreshGraphQuery}/>
          </Col>
        </Row>
        <Row>
          <Col md={12}>
            {this.props.children}
          </Col>
        </Row>
      </Grid>
    );
  }
}

export default withRouter(GraphPanelHeader);

class UniversePicker extends Component {
  render() {
    const {universeItemChanged, universe: {universeList}, selectedUniverse} = this.props;
    var universeItems = universeList.sort((a, b) => a.name.toLowerCase() > b.name.toLowerCase())
                          .map(function(item, idx){
                            return <option key={idx} value={item.universeUUID} name={item.name}>{item.name}</option>
                          });
    var universeOptionArray = [<option key={-1} value="all">All</option>].concat(universeItems);
    var currentUniverseValue = "all";
    if (!_.isString(selectedUniverse)) {
      currentUniverseValue = selectedUniverse.universeUUID;
    }
    return (
      <div className="universe-picker">
        Universe:
        <FormControl componentClass="select" placeholder="select" onChange={universeItemChanged} value={currentUniverseValue}>
          {universeOptionArray}
        </FormControl>
      </div>
    );
  }
}

class NodePicker extends Component {
  render() {
    const {selectedUniverse, nodeItemChanged, selectedNode} = this.props;
    var nodeItems =[];
    if (isValidObject(selectedUniverse) && selectedUniverse!== "all") {
      nodeItems = selectedUniverse.universeDetails.nodeDetailsSet.sort((a, b) => a.nodeName.toLowerCase() > b.nodeName.toLowerCase())
                    .map(function(nodeItem, nodeIdx){
                          return <option key={nodeIdx} value={nodeItem.nodeName}>
                               {nodeItem.nodeName}
                             </option>
                          })
    }
    var nodeOptionArray=[<option key={-1} value="all">All</option>].concat(nodeItems);
    return (
      <div className="node-picker">
        Node:
        <FormControl componentClass="select" onChange={nodeItemChanged} value={selectedNode}>
          {nodeOptionArray}
        </FormControl>
      </div>
    );
  }
}
