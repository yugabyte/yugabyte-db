// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import PropTypes from 'prop-types';

import './stylesheets/YBLoadingIcon.scss';

export default class YBLoadingCircleIcon extends Component {
  static propTypes = {
    size: PropTypes.oneOf(['xsmall', 'small', 'medium', 'large', 'inline']),
    variant: PropTypes.string
  };

  render() {
    const className =
      'yb-loader-circle' + (this.props.size ? ' yb-loader-circle-' + this.props.size : '');
    const variantClass = this.props.variant
      ? `yb-child yb-child-${this.props.variant}`
      : 'yb-child';
    return (
      <div className={className}>
        <div className={`yb-loader-circle1  ${variantClass}`}></div>
        <div className={`yb-loader-circle2  ${variantClass}`}></div>
        <div className={`yb-loader-circle3  ${variantClass}`}></div>
        <div className={`yb-loader-circle4  ${variantClass}`}></div>
        <div className={`yb-loader-circle5  ${variantClass}`}></div>
        <div className={`yb-loader-circle6  ${variantClass}`}></div>
        <div className={`yb-loader-circle7  ${variantClass}`}></div>
        <div className={`yb-loader-circle8  ${variantClass}`}></div>
        <div className={`yb-loader-circle9  ${variantClass}`}></div>
        <div className={`yb-loader-circle10  ${variantClass}`}></div>
        <div className={`yb-loader-circle11  ${variantClass}`}></div>
        <div className={`yb-loader-circle12  ${variantClass}`}></div>
      </div>
    );
  }
}
