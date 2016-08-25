// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';

export default class DescriptionList extends Component {
  static propTypes = {
    listItems: PropTypes.array.isRequired
  };

  render() {
    const { listItems } = this.props;

    const descriptionListItems = listItems.map(function(item, idx) {
      return (
        <span key={idx}>
          <dt>{item.name}</dt>
          <dd>{item.data}</dd>
        </span>);
    });

    return (
      <dl className="dl-horizontal">
        {descriptionListItems}
      </dl>);
  }
}
