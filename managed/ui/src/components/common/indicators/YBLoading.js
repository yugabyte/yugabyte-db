// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import PropTypes from 'prop-types';

import YBLoadingCircleIcon from './YBLoadingCircleIcon';

export default class YBLoading extends Component {
  static propTypes = {
    size: PropTypes.oneOf(['xsmall', 'small', 'medium', 'large', 'inline'])
  };

  render() {
    return (
      <div className="text-center loading-icon-container">
        <YBLoadingCircleIcon size={this.props.size} />
        <div>{this.props.text ? this.props.text : 'Loading'}</div>
      </div>
    );
  }
}
