// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { ToastContainer } from 'react-toastify';
import 'bootstrap/dist/css/bootstrap.css';
import 'react-toastify/dist/ReactToastify.css';
import './stylesheets/App.scss';
import './../_style/fonts.css';

export default class App extends Component {
  render() {
    return (
      <>
        <div>{this.props.children}</div>
        <ToastContainer hideProgressBar position="top-center" />
      </>
    );
  }
}
