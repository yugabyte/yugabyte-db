// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';

export default class DescriptionItem extends Component {

  static propTypes = {
    children: PropTypes.element.isRequired
  };

  render() {
    const {title} = this.props;
    return (
      <div>
        <small className="table-cell-sub-text">{title}</small>
        <div>
          {this.props.children}
        </div>
      </div>
    )
  }
}
