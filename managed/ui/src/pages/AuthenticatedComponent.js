// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import NavBar from '../components/NavBar';
import AuthenticatedComponentContainer from '../containers/AuthenticatedComponentContainer';

export default class AuthenticatedComponent extends Component {
  render() {
    return (
      <AuthenticatedComponentContainer>
        <NavBar />
        {this.props.children}
      </AuthenticatedComponentContainer>
    );
  }
};
