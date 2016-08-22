// Copyright (c) YugaByte, Inc.

import React from 'react';
import { Component } from 'react';
import '../stylesheets/App.css'

export default class App extends Component {
  render() {
    return (
      <div>
        {this.props.children}
      </div>
    );
  }
}
