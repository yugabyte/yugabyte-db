// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import '../stylesheets/TopNavBar.css'

export default class TopNavBar extends Component {
	render() {
    return (
      <nav className="navbar navbar-default navbar-static-top">
        <div className="container-fluid">
          <div className="navbar-header">
            <a className="navbar-brand" href="/">
              YugaByte Customer Portal
            </a>
          </div>
        </div>
      </nav>
	  );
  }
}
