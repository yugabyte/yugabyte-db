// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { DropdownButton, MenuItem } from 'react-bootstrap';

export default class GraphPanelHeader extends Component {
  constructor(props) {
    super(props);
    this.filterChanged = this.filterChanged.bind(this);
  }

  componentWillUnmount() {
    this.props.resetGraphFilter();
  }

  filterChanged(eventKey, event) {
    this.props.updateGraphFilter({
      filterType: event.target.getAttribute("data-filter-type").toLowerCase(),
      filterValue: event.target.getAttribute("data-filter-value")
    });
  }

  render() {
    return (
      <span className="graph-panel-grid">
        <i className="fa fa-bar-chart-o fa-fw"></i>Graph Panel
        <DropdownButton title={"Filter"} id={"graph-filter-dropdown"} pullRight={true} >
          <MenuItem onSelect={this.filterChanged}
                    eventKey="1"
                    data-filter-type="Hour"
                    data-filter-value="1" active>Last 1 Hr</MenuItem>
          <MenuItem onSelect={this.filterChanged}
                    eventKey="2"
                    data-filter-type="Hour"
                    data-filter-value="3">Last 3 Hrs</MenuItem>
          <MenuItem onSelect={this.filterChanged}
                    eventKey="3"
                    data-filter-type="Hour"
                    data-filter-value="6">Last 6 Hrs</MenuItem>
        </DropdownButton>
      </span>);
  }
}
