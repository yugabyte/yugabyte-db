// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Dropdown, MenuItem, Row, Col, Grid , FormControl} from 'react-bootstrap';
import { DateTimePicker } from 'react-widgets';
import { YBButton } from '../../common/forms/fields';
require('react-widgets/dist/css/react-widgets.css');
var moment = require('moment');
import { isValidObject } from '../../../utils/ObjectUtils';
var momentLocalizer = require('react-widgets/lib/localizers/moment');
import './GraphPanelHeader.scss';

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
  nodeName: "all"
}


export default class GraphPanelHeader extends Component {
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
    var currentUniverse = "all";
    var currentUniversePrefix  = "all";
    if (this.props.origin === "universe") {
      currentUniverse = this.props.universe.currentUniverse;
      currentUniversePrefix = currentUniverse.universeDetails.nodePrefix;
    }
    this.state = {
      showDatePicker: false,
      filterLabel: defaultFilter.label,
      filterType: defaultFilter.type,
      currentSelectedUniverse: currentUniverse,
      currentSelectedNode: "all",
      filterParams: {
        endMoment: moment(),
        startMoment: moment().subtract("1", "hours"),
        nodePrefix: currentUniversePrefix,
        nodeName: "all"
      }
    }
  }

  submitGraphFilters(type, val) {
    var queryObject = this.state.filterParams;
    queryObject[type] = val;
    this.props.changeGraphQueryFilters(queryObject);
  }
  handleFilterChange(eventKey, event) {
    var filterInfo = filterTypes[eventKey] || filterTypes[DEFAULT_FILTER_KEY];
    this.setState({
      filterLabel: filterInfo.label,
      filterType: filterInfo.type
    })

    if (event.target.getAttribute("data-filter-type") !== "custom") {
      var endMoment = moment()
      var startMoment = moment().subtract(filterInfo.value, filterInfo.type);
      var newParams = this.state.filterParams;
      newParams.startMoment = startMoment;
      newParams.endMoment = endMoment;
      this.setState({filterParams: newParams});
      this.props.changeGraphQueryFilters(newParams)
    }
  }
  universeItemChanged(event) {
    const {universe: {universeList}} = this.props;
    var self = this;
    var universeFound = false;
    var newParams = this.state.filterParams;
    for (var counter = 0; counter < universeList.length; counter++) {
      if (universeList[counter].universeUUID === event.target.value) {
        self.setState({currentSelectedUniverse: universeList[counter]});
        universeFound = true;
        newParams.nodePrefix = universeList[counter].universeDetails.nodePrefix;
        break;
      }
    }
    if (!universeFound) {
      newParams.nodePrefix = "all";
    }
    this.props.changeGraphQueryFilters(newParams);
    this.setState({filterParams: newParams, currentSelectedNode: "all"})
  }

  nodeItemChanged(event) {
    var newParams = this.state.filterParams;
    newParams.nodeName = event.target.value;
    this.props.changeGraphQueryFilters(newParams);
    this.setState({filterParams: newParams, currentSelectedNode: event.target.value});
  }

  componentDidMount() {
    this.updateGraphQueryParams(this.state.filterParams);
  }

  handleStartDateChange(dateStr) {
    var newParams = this.state.filterParams;
    newParams.startMoment = dateStr;
    this.setState({filterParams: newParams})
  }

  handleEndDateChange(dateStr) {
    var newParams = this.state.filterParams;
    newParams.endMoment = dateStr;
    this.setState({filterParams: newParams})
  }

  applyCustomFilter() {
    this.updateGraphQueryParams(this.state.startMoment, this.state.endMoment)
  }

  updateGraphQueryParams(startMoment, endMoment) {
    var newFilterParams = this.state.filterParams;
    if (isValidObject(startMoment) && isValidObject(endMoment)) {
      newFilterParams.startMoment = startMoment;
      newFilterParams.endMoment = endMoment;
      this.setState({filterParams: newFilterParams})
      this.props.changeGraphQueryFilters(this.state.filterParams);
    }
  }


  render() {
    const { origin } = this.props;
    var datePicker = null;
    if (this.state.filterType === "custom") {
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
      universePicker = <UniversePicker {...this.props} universeItemChanged={this.universeItemChanged}/>;
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

class UniversePicker extends Component {
  render() {
    const {universeItemChanged, universe: {universeList}} = this.props;
    var universeItems = universeList.map(function(item, idx){
      return <option key={idx} value={item.universeUUID} name={item.name}>{item.name}</option>
    });
    var universeOptionArray = [<option key={-1} value="all">All</option>].concat(universeItems);
    return (
      <div className="universe-picker">
        Universe:
        <FormControl componentClass="select" placeholder="select" onChange={universeItemChanged}>
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
      nodeItems = selectedUniverse.universeDetails.nodeDetailsSet.map(function(nodeItem, nodeIdx){
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
