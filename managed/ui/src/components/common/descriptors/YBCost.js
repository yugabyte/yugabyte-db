// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import moment from 'moment';
import { YBFormattedNumber } from '../descriptors';

import './stylesheets/YBCost.css';

const hoursPerDay = 24;

const timeFactor = (base, target) => {
  const timeInHours = {};
  timeInHours[base] = 1;
  timeInHours[target] = 1;

  for (const key of Object.keys(timeInHours)) {
    if (key === 'day') {
      timeInHours[key] = hoursPerDay;
    } else if (key === 'month') {
      timeInHours[key] = hoursPerDay * moment().daysInMonth();
    }
  }

  return timeInHours[target] / timeInHours[base];
};

export default class YBCost extends Component {
  static propTypes = {
    multiplier: PropTypes.oneOf(['day', 'month', 'hour']).isRequired,
    base: PropTypes.oneOf(['day', 'month', 'hour'])
  };

  render() {
    const { value, multiplier, base = 'hour' } = this.props;
    const finalCost = value ? value * timeFactor(base, multiplier) : 0;

    return (
      <YBFormattedNumber
        value={finalCost}
        maximumFractionDigits={2}
        formattedNumberStyle="currency"
        currency="USD"
        multiplier={multiplier}
      />
    );
  }
}
