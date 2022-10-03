/*
 * Created on Wed May 11 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC } from 'react';
import { OverlayTrigger, Popover } from 'react-bootstrap';
import { YBButton } from '../../common/forms/fields';
import clsx from 'clsx';

import './BackupEmpty.scss';

const UPLOAD_ICON = <i className="fa fa-upload backup-empty-icon" />;

interface BackupEmptyProps {
  classNames?: string;
}

export const BackupEmpty: FC<BackupEmptyProps> = ({ children, classNames }) => {
  return <div className={clsx('backup-empty', classNames)}>{children}</div>;
};

export const ScheduledBackupEmpty = ({
  onActionButtonClick,
  disabled = false
}: {
  onActionButtonClick: Function;
  disabled?: boolean;
}) => {
  return (
    <BackupEmpty>
      {UPLOAD_ICON}
      <BackupDisabledTooltip disabled={disabled}>
        <YBButton
          onClick={onActionButtonClick}
          btnClass="btn btn-orange backup-empty-button"
          btnText="Create Scheduled Backup Policy"
          disabled={disabled}
        />
      </BackupDisabledTooltip>
      <div className="sub-text">Currently there are no Scheduled Backup Policies to show</div>
    </BackupEmpty>
  );
};

export const UniverseLevelBackupEmpty = ({
  onActionButtonClick,
  onAdvancedRestoreButtonClick,
  disabled = false
}: {
  onActionButtonClick: Function;
  onAdvancedRestoreButtonClick: Function;
  disabled?: boolean;
}) => {
  return (
    <BackupEmpty>
      <a
        href="#!"
        onClick={(e) => {
          e.preventDefault();
          onAdvancedRestoreButtonClick();
        }}
        className="advanced-restore"
      >
        Advanced Restore
      </a>
      {UPLOAD_ICON}
      <BackupDisabledTooltip disabled={disabled}>
        <YBButton
          onClick={onActionButtonClick}
          btnIcon="fa fa-upload"
          btnClass="btn btn-orange backup-empty-button"
          disabled={disabled}
          btnText="Backup now"
        />
      </BackupDisabledTooltip>
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

const BACKUP_DISABLED_POPOVER = (
  <Popover
    id="popover-backup-disabled"
    title="This universe does not have any tables to backup or backup is disabled"
  />
);

const BackupDisabledTooltip = ({
  disabled,
  children
}: {
  disabled: boolean;
  children: JSX.Element;
}) => {
  return disabled ? (
    <div className="backup-disabled-tooltip">
      <OverlayTrigger trigger="click" placement="top" overlay={BACKUP_DISABLED_POPOVER}>
        <div className="placeholder" />
      </OverlayTrigger>
      {children}
    </div>
  ) : (
    <>{children}</>
  );
};
