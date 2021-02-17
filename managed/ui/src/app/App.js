// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import 'bootstrap/dist/css/bootstrap.css';
import 'react-toastify/dist/ReactToastify.css';
import './stylesheets/App.scss';
import './../_style/fonts.css';
import { ToastContainer } from 'react-toastify';

export default class App extends Component {
  render() {
    return (
      <>
        <div>{this.props.children}</div>
        <ToastContainer/>
      </>
    );
  }
}
