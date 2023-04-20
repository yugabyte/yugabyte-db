/*
 * Created on Fri Feb 03 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import moment from "moment"

export const YBTimeFormats = {
    YB_DEFAULT_TIMESTAMP: 'MMM-DD-YYYY HH:mm:ss ZZ',
    YB_DATE_ONLY_TIMESTAMP: 'MMM-DD-YYYY',
    YB_HOURS_FIRST_TIMESTAMP: 'HH:mm:ss MMM-DD-YYYY [UTC]ZZ',
    YB_ISO8601_TIMESTAMP: 'YYYY-MM-DD[T]H:mm:ssZZ'
} as const;

/**
 * Converts date to RFC3339 format("yyyy-MM-dd'T'HH:mm:ss'Z'")
 * @param d Date
 * @returns RFC3339 format string
 */
export const convertToISODateString = (d: Date) => {
    const pad = (n: number) => { return n < 10 ? '0' + n : n }
    try {
        return d.getUTCFullYear() + '-'
            + pad(d.getUTCMonth() + 1) + '-'
            + pad(d.getUTCDate()) + 'T'
            + pad(d.getUTCHours()) + ':'
            + pad(d.getUTCMinutes()) + ':'
            + pad(d.getUTCSeconds()) + 'Z'
    }
    catch (e) {
        console.error(e);
        return '-';
    }
}

export const ybFormatDate = (d: Date | string | number, timeFormat = YBTimeFormats.YB_DEFAULT_TIMESTAMP) => {
    return moment(d).format(timeFormat as any);
}

export const dateStrToMoment = (str: string) => {
    return moment(str);
}
