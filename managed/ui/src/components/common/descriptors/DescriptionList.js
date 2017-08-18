// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';

import './stylesheets/DescriptionList.css';

export default class DescriptionList extends Component {
  static propTypes = {
    listItems: PropTypes.array.isRequired
  };

  render() {
    const { listItems } = this.props;

    const descriptionListItems = listItems.map(function(item, idx) {
      let itemData = item.data;

      if (Array.isArray(item.data)) {
        const arrData = item.data.map(function(data, dataIdx) {
          return (
            <span key={dataIdx}>
              <dd key={dataIdx}>{data.name}</dd>
            </span>
          );
        });
        itemData = <dl className="dl-nested">{arrData}</dl>;
      }
      return (
        <span key={idx}>
          <dt className={item.nameClass}>{item.name}:</dt>
          <dd className={item.dataClass}>{itemData}</dd>
        </span>);
    });

    return (
      <dl className="dl-normal">
        {descriptionListItems}
      </dl>);
  }
}
