/*
 * Created on Wed May 11 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import clsx from 'clsx';
import React, { FC } from 'react';
import { YBButton } from '../../common/forms/fields';

import './BackupEmpty.scss';

const UPLOAD_ICON = <i className="fa fa-upload backup-empty-icon" />;

interface BackupEmptyProps {
  classNames?: string;
}

const BackupEmpty: FC<BackupEmptyProps> = ({ children, classNames }) => {
  return <div className={clsx('backup-empty', classNames)}>{children}</div>;
};

export const ScheduledBackupEmpty = ({
  onActionButtonClick
}: {
  onActionButtonClick: Function;
}) => {
  return (
    <BackupEmpty>
      {UPLOAD_ICON}
      <YBButton
        onClick={onActionButtonClick}
        btnClass="btn btn-orange backup-empty-button"
        btnText="Create Scheduled Backup Policy"
      />
      <div className="sub-text">Currently there are no Scheduled Backup Policies to show</div>
    </BackupEmpty>
  );
};

export const UniverseLevelBackupEmpty = ({
  onActionButtonClick
}: {
  onActionButtonClick: Function;
}) => {
  return (
    <BackupEmpty>
      {UPLOAD_ICON}
      <YBButton
        onClick={onActionButtonClick}
        btnIcon="fa fa-upload"
        btnClass="btn btn-orange backup-empty-button"
        btnText="Backup now"
      />
      <div className="sub-text">Currently there are no Backups to show</div>
    </BackupEmpty>
  );
};

export const AccountLevelBackupEmpty = () => {
  return (
    <BackupEmpty classNames="account-level-backup">
      <div>
        {UPLOAD_ICON}
        <div className="sub-text">Currently there are no Backups to show</div>
        <div className="sub-text">Backups from all universes will be listed in this page</div>
      </div>
    </BackupEmpty>
  );
};
