/*
 * Created on Tue Mar 15 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useState } from 'react';
import { YBTabsPanel } from '../../panels';
import { BackupList } from '..';
import { DropdownButton, MenuItem, Tab } from 'react-bootstrap';
import { withRouter } from 'react-router';
import { ScheduledBackup } from '../scheduled/ScheduledBackup';
import { useSelector } from 'react-redux';
import { PointInTimeRecovery } from '../pitr/PointInTimeRecovery';
import { isYbcInstalledInUniverse } from '../../../utils/UniverseUtils';
import { BackupThrottleParameters } from '../components/BackupThrottleParameters';
import { BackupAdvancedRestore } from '../components/BackupAdvancedRestore';
import './UniverseLevelBackup.scss';

interface UniverseBackupProps {
  params: {
    uuid: string;
    tab: string;
  };
}

const UniverseBackup: FC<UniverseBackupProps> = ({ params: { uuid } }) => {
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const currentUniverse = useSelector((reduxState: any) => reduxState.universe.currentUniverse);
  const YBCInstalled =
    (featureFlags.test.enableYbc || featureFlags.released.enableYbc) &&
    isYbcInstalledInUniverse(currentUniverse.data.universeDetails);

  const [showAdvancedRestore, setShowAdvancedRestore] = useState(false);
  const [showThrottleParametersModal, setShowThrottleParametersModal] = useState(false);

  return (
    <YBTabsPanel id="backup-tab-panel">
      <Tab eventKey="backupList" title="Backups" unmountOnExit>
        <BackupList allowTakingBackup universeUUID={uuid} />
        <BackupAdvancedRestore
          onHide={() => {
            setShowAdvancedRestore(false);
          }}
          visible={showAdvancedRestore}
          currentUniverseUUID={uuid}
        />
        {YBCInstalled && (
          <BackupThrottleParameters
            visible={showThrottleParametersModal}
            onHide={() => setShowThrottleParametersModal(false)}
            currentUniverseUUID={uuid}
          />
        )}
      </Tab>
      <Tab eventKey="backupSchedule" title="Scheduled Backup Policies" unmountOnExit>
        <ScheduledBackup universeUUID={uuid} />
      </Tab>
      {(featureFlags.test.enablePITR || featureFlags.released.enablePITR) && (
        <Tab eventKey="point-in-time-recovery" title="Point-in-time Recovery" unmountOnExit>
          <PointInTimeRecovery universeUUID={uuid} />
        </Tab>
      )}
      <Tab
        tabClassName="advanced_configs"
        title={
          <DropdownButton
            pullRight
            title={
              <span>
                <i className="fa fa-gear" />
                Advanced
              </span>
            }
            id="advanced_config_but"
          >
            <MenuItem
              onClick={(e) => {
                e.stopPropagation();
                if (currentUniverse?.data?.universeDetails?.universePaused) return;
                setShowAdvancedRestore(true);
              }}
              disabled={currentUniverse?.data?.universeDetails?.universePaused}
            >
              Advanced Restore
            </MenuItem>
            {YBCInstalled && (
              <MenuItem
                onClick={(e) => {
                  e.stopPropagation();
                  setShowThrottleParametersModal(true);
                }}
              >
                Configure Throttle Parameters
              </MenuItem>
            )}
          </DropdownButton>
        }
      ></Tab>
    </YBTabsPanel>
  );
};

export const UniverseLevelBackup = withRouter(UniverseBackup);
