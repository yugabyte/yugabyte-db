// Copyright (c) YugaByte, Inc.
import { isValidObject } from './ObjectUtils';
import { YBFormattedNumber } from '../components/common/descriptors';
import { YBLoadingCircleIcon } from '../components/common/indicators';
import { formatDatetime, ybFormatDate, YBTimeFormats } from '../redesign/helpers/DateUtils';

export function timeFormatter(cell) {
  if (!isValidObject(cell)) {
    return <span>-</span>;
  } else {
    return ybFormatDate(cell);
  }
}

export function timeFormatterISO8601(cell, timezone) {
  if (!isValidObject(cell)) {
    return '<span>-</span>';
  } else {
    return formatDatetime(cell, YBTimeFormats.YB_ISO8601_TIMESTAMP, timezone);
  }
}

export function backupConfigFormatter(row, configList) {
  if (row.storageConfigUUID) {
    const storageConfig = configList.find((config) => config.configUUID === row.storageConfigUUID);
    if (storageConfig) return storageConfig.configName;
  }
  return 'Config UUID (Missing)';
}

export function percentFormatter(cell) {
  return <YBFormattedNumber value={cell / 100} formattedNumberStyle={'percent'} />;
}

export function successStringFormatter(cell, row) {
  switch (row.status) {
    case 'Success':
    case 'Completed':
      return (
        <span className="yb-success-color">
          <i className="fa fa-check" /> Completed
        </span>
      );
    case 'Initializing':
      return (
        <span className="yb-pending-color">
          <YBLoadingCircleIcon size="inline" /> Initializing
        </span>
      );
    case 'InProgress':
      return (
        <span className="yb-pending-color">
          <YBLoadingCircleIcon size="inline" /> InProcess
        </span>
      );
    case 'Running':
      return (
        <span className="yb-pending-color">
          <YBLoadingCircleIcon size="inline" />
          Pending ({percentFormatter(row.percentComplete)})
        </span>
      );
    case 'Failure':
    case 'Failed':
      return (
        <span className="yb-fail-color">
          <i className="fa fa-warning" /> Failed
        </span>
      );
    case 'Deleted':
      return (
        <span className="yb-orange">
          <i className="fa fa-warning" /> Deleted
        </span>
      );
    case 'Abort':
      return (
        <span className="yb-warn-color">
          <i className="fa fa-ban" /> Aborting
        </span>
      );
    case 'Stopped':
    case 'Aborted':
      return (
        <span className="yb-warn-color">
          <i className="fa fa-ban" /> Aborted
        </span>
      );
    default:
      return (
        <span className="yb-fail-color">
          <i className="fa fa-warning" />
          Unknown
        </span>
      );
  }
}

export function alertSeverityFormatter(cell, row) {
  switch (row.severity) {
    case 'SEVERE':
      return (
        <span className="yb-fail-color">
          <i className="fa fa-warning" /> Error
        </span>
      );
    case 'WARNING':
      return (
        <span className="yb-warn-color">
          <i className="fa fa-warning" /> Warning
        </span>
      );
    default:
      return <span>{row.severity}</span>;
  }
}
