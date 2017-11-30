// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';

import './stylesheets/YBLoadingIcon.scss';

export default class YBLoadingIcon extends Component {
  static propTypes = {
    size: PropTypes.oneOf(['xsmall', 'small', 'medium', 'large', 'inline']),
  };

  render() {
    const className = "yb-circle" + (this.props.size ? " yb-circle-"+this.props.size : "");
    return (
      <div className={className}>
        <div className="yb-circle1 yb-child"></div>
        <div className="yb-circle2 yb-child"></div>
        <div className="yb-circle3 yb-child"></div>
        <div className="yb-circle4 yb-child"></div>
        <div className="yb-circle5 yb-child"></div>
        <div className="yb-circle6 yb-child"></div>
        <div className="yb-circle7 yb-child"></div>
        <div className="yb-circle8 yb-child"></div>
        <div className="yb-circle9 yb-child"></div>
        <div className="yb-circle10 yb-child"></div>
        <div className="yb-circle11 yb-child"></div>
        <div className="yb-circle12 yb-child"></div>
      </div>
    );
  }
}
