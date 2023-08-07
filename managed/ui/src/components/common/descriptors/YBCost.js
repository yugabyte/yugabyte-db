// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import PropTypes from 'prop-types';
import moment from 'moment';
import { YBFormattedNumber } from '../descriptors';

import './stylesheets/YBCost.css';

const HOURS_IN_DAY = 24;
const UNKNOWN_COST = '$ -';

const timeFactor = (base, target) => {
  const timeInHours = {};
  timeInHours[base] = 1;
  timeInHours[target] = 1;

  for (const key of Object.keys(timeInHours)) {
    if (key === 'day') {
      timeInHours[key] = HOURS_IN_DAY;
    } else if (key === 'month') {
      timeInHours[key] = HOURS_IN_DAY * moment().daysInMonth();
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
    const { value, multiplier, base = 'hour', isPricingKnown, runtimeConfigs } = this.props;
    const showUICost = runtimeConfigs?.data?.configEntries?.find(
      (c) => c.key === 'yb.ui.show_cost'
    );
    const finalCost = value ? value * timeFactor(base, multiplier) : 0;
    const shouldShowPrice = isPricingKnown && showUICost?.value === 'true';

    return shouldShowPrice ? (
      <YBFormattedNumber
        value={finalCost}
        maximumFractionDigits={2}
        formattedNumberStyle="currency"
        currency="USD"
        multiplier={multiplier}
      />
    ) : (
      <span>{UNKNOWN_COST}</span>
    );
  }
}
