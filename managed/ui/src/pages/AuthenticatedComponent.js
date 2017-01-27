// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import NavBar from '../components/common/nav_bar/NavBar';
import AuthenticatedComponentContainer from '../components/Authenticated/AuthenticatedComponentContainer';

export default class AuthenticatedComponent extends Component {
  render() {
    return (
      <AuthenticatedComponentContainer>
        <NavBar />
        <div className="container-body">
          {this.props.children}
        </div>
      </AuthenticatedComponentContainer>
    );
  }
};
