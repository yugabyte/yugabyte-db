/*
 * Created on Thu Feb 10 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC } from 'react';
import { BACKUP_LABEL_MAP } from '../../backupv2';
import './StatusBadge.scss';

export enum Badge_Types {
  IN_PROGRESS = 'InProgress',
  COMPLETED = 'Completed',
  FAILED = 'Failed',
  DELETED = 'Deleted',
  SKIPPED = 'Skipped',
  STOPPED = 'Stopped',
  FAILED_TO_DELETE = 'FailedToDelete',
  DELETE_IN_PROGRESS = 'DeleteInProgress',
  QUEUED_FOR_DELETION = 'QueuedForDeletion',
  SUCCESS = 'Success',
  CREATED = 'Created',
}

interface StatusBadgeProps extends React.HTMLAttributes<HTMLDivElement> {
  statusType: Badge_Types;
  customLabel?: string;
}

const getIcon = (statusType: Badge_Types) => {
  let icon = '';
  switch (statusType) {
    case Badge_Types.COMPLETED:
    case Badge_Types.SUCCESS:
      icon = 'fa-check';
      break;
    case Badge_Types.FAILED:
    case Badge_Types.FAILED_TO_DELETE:
      icon = 'fa-exclamation-circle';
      break;

    case Badge_Types.DELETE_IN_PROGRESS:
    case Badge_Types.IN_PROGRESS:
      icon = 'fa-spinner fa-pulse';
      break;
    case Badge_Types.QUEUED_FOR_DELETION:
      icon = 'fa-clock-o';
      break;
  }
  return icon ? <i className={`fa ${icon} badge-icon ${statusType}`} /> : null;
};

export const StatusBadge: FC<StatusBadgeProps> = ({ statusType, customLabel, ...others }) => {
  const label = customLabel ?? BACKUP_LABEL_MAP[statusType];
  return (
    <span {...others} className={`status-badge ${statusType}`}>
      {label} 
      {getIcon(statusType)}
    </span>
  );
};
