// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

export default class YBPanelItem extends Component {

  render() {
    return (
      <div>
        <div className="row">
          <div className="col-md-12">
            <div className="x_panel">
              <div className="x_title">
                <h2>{this.props.name}</h2>
                <ul className="nav navbar-right panel_toolbox">
                  <li><a className="collapse-link"><i className="fa fa-chevron-up"></i></a>
                  </li>
                  <li className="dropdown">
                    <a href="#" className="dropdown-toggle" data-toggle="dropdown" role="button"
                       aria-expanded="false"><i className="fa fa-wrench"></i></a>
                    <ul className="dropdown-menu" role="menu">
                      <li><a href="#">Settings 1</a>
                      </li>
                      <li><a href="#">Settings 2</a>
                      </li>
                    </ul>
                  </li>
                  <li><a className="close-link"><i className="fa fa-close"></i></a>
                  </li>
                </ul>
                <div className="clearfix"></div>
              </div>
              {this.props.children}
            </div>
          </div>
        </div>
      </div>
    );
  }
}

