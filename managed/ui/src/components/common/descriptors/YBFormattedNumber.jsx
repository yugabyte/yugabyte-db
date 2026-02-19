// Copyright (c) YugabyteDB, Inc.

import { Component } from 'react';
import { formatNumber } from '../../../utils/Formatters';
import { isFinite } from 'lodash';

export default class YBFormattedNumber extends Component {
  render() {
    const {
      value,
      formatStyle = 'decimal',
      maximumFractionDigits,
      currency = 'USD',
      ...otherProps
    } = this.props;

    if (!isFinite(value)) {
      return <span>n/a</span>;
    }

    const formattedValue = formatNumber(value, {
      formatOptions: {
        maximumFractionDigits: maximumFractionDigits ?? 2,
        style: formatStyle,
        currency: currency
      }
    });

    return <span {...otherProps}>{formattedValue}</span>;
  }
}
