/*
 * Created on Fri Feb 18 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useState } from 'react';
import moment from 'moment';
import { useSelector } from 'react-redux';
import { keyBy, mapValues, capitalize, lowerCase } from 'lodash';
import { Backup_Options_Type, IBackup, IStorageConfig, IUniverse } from './IBackup';
import { Backup_States } from '../common/IBackup';
import { Alert } from 'react-bootstrap';
import { TableType } from '../../../redesign/helpers/dtos';
import './BackupUtils.scss';
import { MILLISECONDS_IN } from '../scheduled/ScheduledBackupUtils';

export const BACKUP_REFETCH_INTERVAL = 20 * 1000;

/**
 * Calculates the difference between two dates
 * @param startTime start time
 * @param endtime end time
 * @returns diff between the dates
 */
export const calculateDuration = (
  startTime: string,
  endtime: string,
  haveSeparator = false
): string => {
  const start = moment(startTime);
  const end = moment(endtime);

  if (start.isSame(end)) return '0 s';

  const totalDays = end.diff(start, 'days');
  const totalHours = end.diff(start, 'hours');
  const totalMinutes = end.diff(start, 'minutes');
  const totalSeconds = end.diff(start, 'seconds');
  let duration = totalDays !== 0 ? (haveSeparator ? `${totalDays}d ` : `${totalDays} d `) : '';
  duration +=
    totalHours % 24 !== 0 ? (haveSeparator ? `${totalHours % 24}h ` : `${totalHours % 24} h `) : '';
  duration +=
    totalMinutes % 60 !== 0
      ? haveSeparator
        ? `${totalMinutes % 60}m `
        : `${totalMinutes % 60} m `
      : ``;
  duration +=
    totalSeconds % 60 !== 0
      ? haveSeparator
        ? `${totalSeconds % 60}s`
        : `${totalSeconds % 60} s`
      : '';
  return duration;
};

export const BACKUP_STATUS_OPTIONS: { value: Backup_States | null; label: string }[] = [
  {
    label: 'All',
    value: null
  },
  {
    label: 'In Progress',
    value: Backup_States.IN_PROGRESS
  },
  {
    label: 'Completed',
    value: Backup_States.COMPLETED
  },
  {
    label: 'Delete In Progress',
    value: Backup_States.DELETE_IN_PROGRESS
  },
  {
    label: 'Failed',
    value: Backup_States.FAILED
  },
  {
    label: 'Failed To Delete',
    value: Backup_States.FAILED_TO_DELETE
  },
  {
    label: 'Queued For Deletion',
    value: Backup_States.QUEUED_FOR_DELETION
  },
  {
    label: 'Skipped',
    value: Backup_States.SKIPPED
  },
  {
    label: 'Cancelled',
    value: Backup_States.STOPPED
  }
];

export const DATE_FORMAT = 'YYYY-MM-DD HH:mm:ss';
export const KEYSPACE_VALIDATION_REGEX = /^[A-Za-z_][A-Za-z_0-9$]*$/;

export const formatUnixTimeStamp = (unixTimeStamp: number) =>
  moment(unixTimeStamp).format(DATE_FORMAT);

export const RevealBadge = ({ label, textToShow }: { label: string; textToShow: string }) => {
  const [reveal, setReveal] = useState(false);
  return (
    <span className="reveal-badge">
      {reveal ? (
        <span onClick={() => setReveal(false)}>{textToShow}</span>
      ) : (
        <span onClick={() => setReveal(true)}>{label}</span>
      )}
    </span>
  );
};

export const FormatUnixTimeStampTimeToTimezone = ({ timestamp }: { timestamp: any }) => {
  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);
  if (!timestamp) return <span>-</span>;
  const formatTime = (currentUserTimezone
    ? (moment.utc(timestamp) as any).tz(currentUserTimezone)
    : moment.utc(timestamp)
  ).format('YYYY-MM-DD H:mm:ss');
  return <span>{formatTime}</span>;
};

export const ENTITY_NOT_AVAILABLE = (
  <span className="alert-message warning">
    Not Available <i className="fa fa-warning" />
  </span>
);
export const SPINNER_ICON = <i className="fa fa-spinner fa-pulse" />;

export const CALDENDAR_ICON = () => ({
  alignItems: 'center',
  display: 'flex',

  ':before': {
    backgroundColor: 'white',
    borderRadius: 10,
    fontFamily: 'FontAwesome',
    content: '"\f133"',
    display: 'block',
    marginRight: 8
  }
});

export const convertArrayToMap = (arr: IUniverse[], keyStr: string, valueStr: string) =>
  mapValues(keyBy(arr, keyStr), valueStr);

export const ParallelThreads = {
  MIN: 1,
  XCLUSTER_DEFAULT: 8,
  MAX: 100
};

export const BACKUP_IN_PROGRESS_MSG = <Alert bsStyle="success">Backup is in progress.</Alert>;
export const RESTORE_IN_PROGRESS_MSG = (
  <Alert bsStyle="info">
    Restore is in progress. No cluster configuration changes can be done when restore is in
    progress.
  </Alert>
);
export const convertBackupToFormValues = (backup: IBackup, storage_config: IStorageConfig) => {
  const formValues = {
    use_cron_expression: false,
    cron_expression: '',
    api_type: {
      value: backup.backupType,
      label: backup.backupType === TableType.PGSQL_TABLE_TYPE ? 'YSQL' : 'YCQL'
    },
    selected_ycql_tables: [] as any[],
    parallel_threads: ParallelThreads.MIN,
    storage_config: null as any,
    baseBackupUUID: backup.commonBackupInfo.baseBackupUUID,
    isTableByTableBackup: backup.commonBackupInfo.tableByTableBackup,
    useTablespaces: backup.useTablespaces
  };
  if (backup.isFullBackup) {
    formValues['db_to_backup'] = {
      value: null,
      label: `All ${backup.backupType === TableType.PGSQL_TABLE_TYPE ? 'Databases' : 'Keyspaces'}`
    };
  } else {
    formValues['db_to_backup'] = backup.commonBackupInfo.responseList.map((k) => {
      return { value: k.keyspace, label: k.keyspace };
    })[0];

    if (backup.backupType === TableType.YQL_TABLE_TYPE) {
      formValues['backup_tables'] =
        backup.commonBackupInfo.responseList.length > 0 &&
        backup.commonBackupInfo.responseList[0].allTables
          ? Backup_Options_Type.ALL
          : Backup_Options_Type.CUSTOM;

      if (formValues['backup_tables'] === Backup_Options_Type.CUSTOM) {
        backup.commonBackupInfo.responseList.forEach((k: any) => {
          k.tablesList.forEach((table: string, index: number) => {
            formValues['selected_ycql_tables'].push({
              tableName: table,
              keySpace: k.keyspace,
              isIndexTable: k.isIndexTable
            });
          });
        });
      }
    }
  }

  if (storage_config) {
    formValues['storage_config'] = {
      label: storage_config.configName,
      value: storage_config.configUUID,
      name: storage_config.name
    };
  }

  if (backup.expiryTime) {
    formValues['retention_interval'] = Math.ceil(
      (backup.hasIncrementalBackups
        ? Date.parse(backup.expiryTime) - Date.parse(backup.lastIncrementalBackupTime)
        : Date.parse(backup.expiryTime) - Date.parse(backup.commonBackupInfo.createTime)) /
        MILLISECONDS_IN[backup.expiryTimeUnit]
    );
    const interval_type = capitalize(lowerCase(backup.expiryTimeUnit));
    formValues['retention_interval_type'] = { value: interval_type, label: interval_type };
    formValues['keep_indefinitely'] = false;
  } else {
    formValues['keep_indefinitely'] = true;
  }

  formValues['scheduleName'] = backup.scheduleName;
  formValues['backupObj'] = backup;

  return formValues;
};
