// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Dropdown, MenuItem, Row, Col, Grid } from 'react-bootstrap';
import { DateTimePicker } from 'react-widgets';
import { YBButton, YBMultiSelect } from '../../common/forms/fields';
import moment from 'moment';
import { isValidObject } from '../../../utils/ObjectUtils';
var momentLocalizer = require('react-widgets/lib/localizers/moment');
require('react-widgets/dist/css/react-widgets.css');
import {Field} from 'redux-form';
import './GraphPanelHeader.scss'

// We can define different filter types here, the type parameter should be
// valid type that moment supports except for custom and divider.
// if the filter type has a divider, we would just add a divider in the dropdown
// and custom filter would show custom date picker
const filterTypes = [
  {label: "Last 1 hr", type: "hours", value: "1"},
  {label: "Last 6 hrs", type: "hours", value: "6"},
  {label: "Last 12 hrs", type: "hours", value: "12"},
  {type: "divider"},
  {label: "Custom", type: "custom"}
]

const DEFAULT_FILTER_KEY = 0
export const DEFAULT_GRAPH_FILTER = {
  startDate: moment().subtract(
    filterTypes[DEFAULT_FILTER_KEY].value,
    filterTypes[DEFAULT_FILTER_KEY].type),
  endDate: moment()
}


export default class GraphPanelHeader extends Component {
  constructor(props) {
    momentLocalizer(moment);
    super(props);
    this.handleFilterChange = this.handleFilterChange.bind(this);
    this.handleStartDateChange = this.handleStartDateChange.bind(this);
    this.handleEndDateChange = this.handleEndDateChange.bind(this);
    this.applyCustomFilter = this.applyCustomFilter.bind(this);
    this.updateGraphQueryParams = this.updateGraphQueryParams.bind(this);
    var defaultFilter = filterTypes[DEFAULT_FILTER_KEY];

    this.state = {
      showDatePicker: false,
      filterLabel: defaultFilter.label,
      filterType: defaultFilter.type,
      endMoment: moment(),
      startMoment: moment().subtract(defaultFilter.value, defaultFilter.type)
    }
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
      this.setState({startMoment: startMoment, endMoment: endMoment})
      this.updateGraphQueryParams(startMoment, endMoment)
    }
  }

  componentDidMount() {
    this.updateGraphQueryParams(this.state.startMoment, this.state.endMoment)
  }
  handleStartDateChange(dateStr) {
    this.setState({startMoment: moment(dateStr)})
  }

  handleEndDateChange(dateStr) {
    this.setState({endMoment: moment(dateStr)})
  }

  applyCustomFilter() {
      this.updateGraphQueryParams(this.state.startMoment, this.state.endMoment)
  }

  updateGraphQueryParams(startMoment, endMoment) {
    this.props.changeGraphQueryPeriod({
      startDate: startMoment.format('X'),
      endDate: endMoment.format('X')
    })
  }

  render() {
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

    var universePicker = <span/>;
    var universeSelectionChanged = function(universeVals) {
      // TODO Add Universe Filter Logic Here
    }
    if (isValidObject(this.props.origin) && this.props.origin === "customer") {
      var universeItems = this.props.universe.universeList.map(function(item, idx){
        return {"label": item.name, "value": item.universeDetails.nodePrefix}
      });
      universePicker = <Col md={3}><Field name="universeSelect" component={YBMultiSelect}
                         options={universeItems} selectValChanged={universeSelectionChanged}
                         multi={true}/>
                       </Col>
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
        </MenuItem>)
    });


    return (
      <Grid className="x_panel graph-panel">
        <Row className="x_title">
          <Col md={6}>
            <h2>Metrics</h2>
          </Col>
          <Col md={6}>
            <form name="GraphPanelFilterForm">
              {universePicker}
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
