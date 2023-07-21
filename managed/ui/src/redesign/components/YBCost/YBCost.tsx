import { FC } from 'react';
import moment from 'moment';
import { YBFormattedNumber } from '../../../components/common/descriptors'; //TODO : Migrate react-intl to new version

export enum Multiplier {
  HOUR = 'hour',
  DAY = 'day',
  MONTH = 'month'
}

interface YBCostProps {
  multiplier: Multiplier;
  value: number;
  base?: Multiplier;
  isPricingKnown?: boolean;
}

const HOURS_IN_DAY = 24;
const UNKNOWN_COST = '$ -';

const timeFactor = (base: Multiplier, target: Multiplier) => {
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

export const YBCost: FC<YBCostProps> = ({
  isPricingKnown,
  value,
  multiplier,
  base = Multiplier.HOUR
}) => {
  const finalCost = value ? value * timeFactor(base, multiplier) : 0;
  return isPricingKnown ? (
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
};
