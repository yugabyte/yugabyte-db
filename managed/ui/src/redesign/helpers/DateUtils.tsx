/*
 * Created on Fri Feb 03 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import moment from 'moment-timezone';
import { useSelector } from 'react-redux';
import { isInteger } from 'lodash';

export const YBTimeFormats = {
  YB_DEFAULT_TIMESTAMP: 'MMM-DD-YYYY HH:mm:ss [UTC]ZZ',
  YB_DATE_ONLY_TIMESTAMP: 'MMM-DD-YYYY',
  YB_HOURS_FIRST_TIMESTAMP: 'HH:mm:ss MMM-DD-YYYY [UTC]ZZ',
  YB_ISO8601_TIMESTAMP: 'YYYY-MM-DD[T]HH:mm:ssZZ',
  YB_TIME_ONLY_TIMESTAMP: 'HH:mm:ss'
} as const;

export type YBTimeFormats = typeof YBTimeFormats[keyof typeof YBTimeFormats];

export const YB_INPUT_TIMESTAMP_FORMAT = 'ddd MMM DD HH:mm:ss z YYYY';

export const YB_LIVE_QUERY_TIMESTAMP_FORMAT = 'YYYY-MM-DD HH:mm:ss:SSSZ';

/**
 * Converts date to RFC3339 format("yyyy-MM-dd'T'HH:mm:ss'Z'")
 * @param d Date
 * @returns RFC3339 format string
 */
export const convertToISODateString = (d: Date) => {
  const pad = (n: number) => {
    return n < 10 ? '0' + n : n;
  };
  try {
    return (
      d.getUTCFullYear() +
      '-' +
      pad(d.getUTCMonth() + 1) +
      '-' +
      pad(d.getUTCDate()) +
      'T' +
      pad(d.getUTCHours()) +
      ':' +
      pad(d.getUTCMinutes()) +
      ':' +
      pad(d.getUTCSeconds()) +
      'Z'
    );
  } catch (e) {
    console.error(e);
    return '-';
  }
};

/**
 * Format the provided datetime string using one of our standard YBA datetime formats.
 */
export const formatDatetime = (
  date: moment.MomentInput,
  timeFormat: YBTimeFormats = YBTimeFormats.YB_DEFAULT_TIMESTAMP,
  timezone?: string,
  inputTimeFormat?: string
): string => {
  const momentObj = getMomentObject(date, inputTimeFormat);
  return timezone ? momentObj.tz(timezone).format(timeFormat) : momentObj.format(timeFormat);
};

/**
 * Custom hook that returns a function to format datetime with user timezone from Redux
 */
export const useFormatDatetime = () => {
  const userTimezone = useSelector((state: any) => state.customer?.currentUser?.data?.timezone);

  return (
    date: moment.MomentInput,
    timeFormat: YBTimeFormats = YBTimeFormats.YB_DEFAULT_TIMESTAMP,
    inputTimeFormat?: string
  ): string => {
    return formatDatetime(date, timeFormat, userTimezone, inputTimeFormat);
  };
};

type FormatDateProps = {
  date: Date | string | number;
  timeFormat: YBTimeFormats;
  timezone?: string;
};

export const YBFormatDate: FC<FormatDateProps> = ({ date, timeFormat, timezone }) => {
  const userTimezone = useSelector((state: any) => state.customer?.currentUser?.data?.timezone);
  const currentUserTimezone = timezone ?? userTimezone;
  return <>{formatDatetime(date, timeFormat, currentUserTimezone)}</>;
};

export const ybFormatDate = (date: Date | string | number, timeFormat?: YBTimeFormats) => {
  const defaultFormat = timeFormat ?? YBTimeFormats.YB_DEFAULT_TIMESTAMP;
  return <YBFormatDate date={date} timeFormat={defaultFormat} />;
};

export const dateStrToMoment = (str: string) => {
  return getMomentObject(str);
};

export const getDiffHours = (startDateTime: any, endDateTime: any) => {
  const diffHours = (endDateTime - startDateTime) / 3600000;
  return diffHours;
};

const getMomentObject = (date: moment.MomentInput, inputTimeFormat = YB_INPUT_TIMESTAMP_FORMAT) => {
  //charts use linux epoch as timestamps
  return !isInteger(date) && moment(date, inputTimeFormat).isValid()
    ? moment(date, inputTimeFormat)
    : moment(date);
};

export const getBrowserTimezoneOffset = (): string => {
  const offset = new Date().getTimezoneOffset();
  const sign = offset <= 0 ? '+' : '-';
  const absOffset = Math.abs(offset);
  const hours = Math.floor(absOffset / 60)
    .toString()
    .padStart(2, '0');
  const minutes = (absOffset % 60).toString().padStart(2, '0');
  return `UTC${sign}${hours}${minutes}`;
};
