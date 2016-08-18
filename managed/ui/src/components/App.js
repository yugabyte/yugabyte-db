// Copyright (c) YugaByte, Inc.

import React from 'react';
import { Component } from 'react';
import '../stylesheets/App.css'

export default class App extends Component {
	componentWillMount() {
    this.props.loadCustomerFromToken();
  }
  render() {
    return (
      <div>
        {this.props.children}
      </div>
    );
  }
}
