// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';

import './stylesheets/DescriptionList.css'

export default class DescriptionItem extends Component {

  static propTypes = {
    children: PropTypes.element.isRequired
  };
  render() {
    const {title} = this.props;
    return (
      <div className="description-item clearfix">
        <div className="description-item-text">
          {this.props.children}
        </div>
        <small className="description-item-sub-text">{title}</small>
      </div>
    )
  }
}
