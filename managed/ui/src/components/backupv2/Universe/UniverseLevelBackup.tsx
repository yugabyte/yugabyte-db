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

interface UniverseBackupProps {
  params: {
    uuid: string;
    tab: string;
  };
}

const UniverseBackup: FC<UniverseBackupProps> = ({ params: { uuid } }) => {
  return (
    <YBTabsPanel id="backup-tab-panel">
      <Tab eventKey="backupList" title="Backups" unmountOnExit>
        <BackupList allowTakingBackup universeUUID={uuid} />
      </Tab>
    </YBTabsPanel>
  );
};

export const UniverseLevelBackup = withRouter(UniverseBackup);
