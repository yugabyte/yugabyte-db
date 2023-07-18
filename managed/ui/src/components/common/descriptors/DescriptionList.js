// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import PropTypes from 'prop-types';
import { isNonEmptyString } from '../../../utils/ObjectUtils';

import './stylesheets/DescriptionList.css';

export default class DescriptionList extends Component {
  static propTypes = {
    listItems: PropTypes.array.isRequired,
    type: PropTypes.string
  };

  static defaultProps = {
    type: 'normal'
  };

  render() {
    const { listItems, type, className } = this.props;

    const classNameResult = 'dl-' + type + (className ? ' ' + className : '');

    const descriptionListItems = listItems.map(function (item, idx) {
      let itemData = item.data;

      if (Array.isArray(item.data)) {
        const arrData = item.data.map(function (data, dataIdx) {
          return (
            // eslint-disable-next-line react/no-array-index-key
            <span key={dataIdx}>
              {/* eslint-disable-next-line react/no-array-index-key */}
              <dd key={dataIdx}>{data.name}</dd>
            </span>
          );
        });
        itemData = <dl className="dl-nested">{arrData}</dl>;
      }
      return (
        // eslint-disable-next-line react/no-array-index-key
        <span key={idx}>
          {isNonEmptyString(item.name) && <dt className={item.nameClass}>{item.name}:</dt>}
          <dd className={item.dataClass}>{itemData}</dd>
        </span>
      );
    });

    return <dl className={classNameResult}>{descriptionListItems}</dl>;
  }
}
