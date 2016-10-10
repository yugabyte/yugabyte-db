// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

export default class AuthenticatedComponent extends Component {
  componentWillMount() {
    this.props.fetchUniverseList();
    this.props.getProviderListItems();
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
