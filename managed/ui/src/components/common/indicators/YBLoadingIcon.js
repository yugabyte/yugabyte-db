// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import './stylesheets/YBLoadingIcon.scss'

export default class YBLoadingIcon extends Component {
  render() {
    return (
      <div className="text-center loading-icon-container">
        <i className="fa fa-spinner fa-spin"/>
        <div>Loading</div>
      </div>
    )
  }
}
