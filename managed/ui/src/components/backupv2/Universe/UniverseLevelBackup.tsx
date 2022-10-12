/*
 * Created on Tue Mar 15 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC } from 'react';
import { YBTabsPanel } from '../../panels';
import { BackupList } from '..';
import { Tab } from 'react-bootstrap';
import './UniverseLevelBackup.scss';
import { withRouter } from 'react-router';
import { ScheduledBackup } from '../scheduled/ScheduledBackup';
import { useSelector } from 'react-redux';
import { PointInTimeRecovery } from '../pitr/PointInTimeRecovery';
import './UniverseLevelBackup.scss';

interface UniverseBackupProps {
  params: {
    uuid: string;
    tab: string;
  };
}

const UniverseBackup: FC<UniverseBackupProps> = ({ params: { uuid } }) => {
  const featureFlags = useSelector((state: any) => state.featureFlags);
  return (
    <YBTabsPanel id="backup-tab-panel">
      <Tab eventKey="backupList" title="Backups" unmountOnExit>
        <BackupList allowTakingBackup universeUUID={uuid} />
      </Tab>
      <Tab eventKey="backupSchedule" title="Scheduled Backup Policies" unmountOnExit>
        <ScheduledBackup universeUUID={uuid} />
      </Tab>
      {(featureFlags.test.enablePITR || featureFlags.released.enablePITR) && (
        <Tab eventKey="point-in-time-recovery" title="Point-in-time Recovery" unmountOnExit>
          <PointInTimeRecovery universeUUID={uuid} />
        </Tab>
      )}
    </YBTabsPanel>
  );
};

export const UniverseLevelBackup = withRouter(UniverseBackup);
