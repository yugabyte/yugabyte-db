// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { withRouter } from 'react-router';
import { Tabs } from 'react-bootstrap';

class YBTabsPanel extends Component {
  constructor(props) {
    super(props);
    this.tabSelect = this.tabSelect.bind(this);
  }

  tabSelect(selectedKey) {
    // Update the query params.
    let currentLocation = this.props.location;
    currentLocation.query = { tab: selectedKey }
    this.props.router.push(currentLocation)
  }

  static propTypes = {
    id: PropTypes.string.isRequired,
    activeTab: PropTypes.string.isRequired,
    children: PropTypes.array.isRequired
  }

  render() {
    let currentLocation = this.props.location;
    var activeTabKey = this.props.activeTab;
    if (currentLocation.query.tab !== "") {
      activeTabKey = currentLocation.query.tab
    }

    return (
      <Tabs activeKey={activeTabKey} onSelect={this.tabSelect} id={this.props.id}>
        {this.props.children}
      </Tabs>
    )
  }
}

export default withRouter(YBTabsPanel)
