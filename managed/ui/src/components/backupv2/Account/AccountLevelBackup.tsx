/*
 * Created on Thu Feb 10 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { Tab } from 'react-bootstrap';
import { useSelector } from 'react-redux';
import { BackupList, Restore } from '..';
import { YBTabsPanel } from '../../panels';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacValidator';
import { UserPermissionMap } from '../../../redesign/features/rbac/UserPermPathMapping';
import './AccountLevelBackup.scss';

export const AccountLevelBackup: FC = () => {
  const featureFlags = useSelector((state: any) => state.featureFlags);

  if (featureFlags.test.enableRestore || featureFlags.released.enableRestore) {
    return (
      <RbacValidator accessRequiredOn={{
        ...UserPermissionMap.listBackup
      }}
      >
        <YBTabsPanel id="account-level-backup-tab-panel" defaultTab="backupList">
          <Tab eventKey="backupList" title="Backups" unmountOnExit>
            <BackupList />
          </Tab>
          <Tab eventKey="restoreList" title="Restore History" unmountOnExit>
            <Restore type="ACCOUNT_LEVEL" />
          </Tab>
        </YBTabsPanel>
      </RbacValidator>
    );
  }

  return <RbacValidator accessRequiredOn={{
    ...UserPermissionMap.listBackup
  }}><BackupList /></RbacValidator>;
};
