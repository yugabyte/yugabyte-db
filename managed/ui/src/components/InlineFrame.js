// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';

export default class InlineFrame extends Component {
  static propTypes = {
    src: PropTypes.string.isRequired,
    width: PropTypes.number,
    height: PropTypes.number
  };

  static defaultProps = {
    width: 450,
    height: 200
  }

  render() {
    const { src, height, width } = this.props;

    return (
      <iframe src={src} width={width} height={height} {...this.props} />);
  }
}
