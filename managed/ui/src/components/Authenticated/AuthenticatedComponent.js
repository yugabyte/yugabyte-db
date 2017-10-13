// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {withRouter} from 'react-router';
const PropTypes = require('prop-types');

class AuthenticatedComponent extends Component {
  constructor(props) {
    super(props);
    this.state = {prevPath: ""};
  }

  getChildContext() {
    return {prevPath: this.state.prevPath};
  }

  componentWillMount() {
    this.props.fetchHostInfo();
    this.props.fetchSoftwareVersions();
    this.props.fetchTableColumnTypes();
    this.props.fetchUniverseList();
    this.props.getEBSListItems();
    this.props.getProviderListItems();
    this.props.getSupportedRegionList();
    this.props.getYugawareVersion();
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
    if (this.props.location !== nextProps.location) {
      this.setState({prevPath: this.props.location.pathname});
    }
  }

  render() {
    return (
      <div>
        {this.props.children}
      </div>
    );
  }
}

export default withRouter(AuthenticatedComponent);

AuthenticatedComponent.childContextTypes = {
  prevPath: PropTypes.string
};
