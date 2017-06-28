// Copyright (c) YugaByte, Inc.

import React from 'react';
import { Component } from 'react';
import 'babel-polyfill';
import 'bootstrap/dist/css/bootstrap.css';
import './stylesheets/App.scss';

export default class App extends Component {
  render() {
    return (
      <div>
        {this.props.children}
      </div>
    );
  }
}
