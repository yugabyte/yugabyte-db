// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { withRouter } from 'react-router';
import { Tabs } from 'react-bootstrap';

class YBTabsPanel extends Component {
  tabSelect = selectedKey => {
    const currentLocation = this.props.location;
    if (this.props.routePrefix) {
      currentLocation.pathname = this.props.routePrefix + selectedKey;
    } else {
      currentLocation.query = currentLocation.query || {};
      currentLocation.query.tab = selectedKey;
    }
    this.props.router.push(currentLocation);
  };

  static propTypes = {
    id: PropTypes.string.isRequired,
    activeTab: PropTypes.string,
    defaultTab: PropTypes.string.isRequired,
    children: PropTypes.oneOfType([PropTypes.object, PropTypes.array]),
    className: PropTypes.string,
    routePrefix: PropTypes.string,
  }

  render() {
    const {activeTab, defaultTab, location} = this.props;
    const activeTabKey = activeTab || location.query.tab || defaultTab;
    return (
      <Tabs activeKey={activeTabKey} onSelect={this.tabSelect} id={this.props.id} className={this.props.className}>
        {this.props.children}
      </Tabs>
    );
  }
}

export default withRouter(YBTabsPanel);
