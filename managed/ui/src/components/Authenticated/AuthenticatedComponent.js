// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

export default class AuthenticatedComponent extends Component {

  componentWillMount() {
    this.props.fetchHostInfo();
    this.props.fetchSoftwareVersions();
    this.props.fetchUniverseList();
    this.props.getEBSListItems();
    this.props.getProviderListItems();
    this.props.getSupportedRegionList();
  }

  componentWillUnmount() {
    this.props.resetUniverseList();
  }

  render() {
    return (
      <div>
        {this.props.children}
      </div>
    )
  }
}
