// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

export default class Tasks extends Component {
  render() {
    return (
      <div>
        {this.props.children}
      </div>
    );
  }
}
