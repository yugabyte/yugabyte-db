import { FC } from 'react';
import moment from 'moment-timezone';

export const YBTimeFormats = {
  YB_DEFAULT_TIMESTAMP: 'MMM-DD-YYYY HH:mm:ss [UTC]ZZ',
  YB_DATE_ONLY_TIMESTAMP: 'MMM-DD-YYYY',
  YB_HOURS_FIRST_TIMESTAMP: 'HH:mm:ss MMM-DD-YYYY [UTC]ZZ',
  YB_ISO8601_TIMESTAMP: 'YYYY-MM-DD[T]H:mm:ssZZ',
  YB_TIME_ONLY_TIMESTAMP: 'HH:mm:ss',
  YB_DATE_TIME_TIMESTAMP: 'YYYY-MM-DDThh:mm'
} as const;
// eslint-disable-next-line no-redeclare
export type YBTimeFormats = typeof YBTimeFormats[keyof typeof YBTimeFormats];

type FormatDateProps = {
  date: Date | string | number;
  timeFormat: YBTimeFormats;
};

export const ybFormatDate = (
  date: Date | string | number,
  timeFormat = YBTimeFormats.YB_DEFAULT_TIMESTAMP
) => {
  return <YBFormatDate date={date} timeFormat={timeFormat} />;
};

export const formatDatetime = (
  date: moment.MomentInput,
  timeFormat: YBTimeFormats = YBTimeFormats.YB_DEFAULT_TIMESTAMP,
  timezone?: string
): string => {
  return timezone ? moment(date).tz(timezone).format(timeFormat) : moment(date).format(timeFormat);
};

export const YBFormatDate: FC<FormatDateProps> = ({ date, timeFormat }) => {
  return <>{formatDatetime(date, timeFormat)}</>;
};
