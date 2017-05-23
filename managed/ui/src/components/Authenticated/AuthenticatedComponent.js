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

  componentWillReceiveProps(nextProps) {
    if (this.props.fetchMetadata !== nextProps.fetchMetadata && nextProps.fetchMetadata) {
      this.props.getProviderListItems();
      this.props.fetchUniverseList();
      this.props.getSupportedRegionList();
    }
    if (this.props.fetchUniverseMetadata !== nextProps.fetchUniverseMetadata && nextProps.fetchUniverseMetadata) {
      this.props.fetchUniverseList();
    }
  }

  render() {
    return (
      <div>
        {this.props.children}
      </div>
    )
  }
}
