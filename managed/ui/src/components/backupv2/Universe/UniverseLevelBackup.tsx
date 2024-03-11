/*
 * Created on Tue Mar 15 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import { DropdownButton, MenuItem, Tab } from 'react-bootstrap';
import { withRouter } from 'react-router';
import { useSelector } from 'react-redux';
import { BackupList, Restore } from '..';
import { YBTabsPanel } from '../../panels';
import { ScheduledBackup } from '../scheduled/ScheduledBackup';
import { BackupAndRestoreBanner } from '../restore/BackupAndRestoreBanner';
import { PointInTimeRecovery } from '../pitr/PointInTimeRecovery';
import { isYbcInstalledInUniverse, getPrimaryCluster } from '../../../utils/UniverseUtils';
import { BackupThrottleParameters } from '../components/BackupThrottleParameters';
import { AdvancedRestoreNewModal } from '../components/advancedRestore/AdvancedRestoreNewModal';
import { BackupAdvancedRestore } from '../components/BackupAdvancedRestore';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { Universe } from '../../../redesign/helpers/dtos';
import { compareYBSoftwareVersions } from '../../../utils/universeUtilsTyped';

import './UniverseLevelBackup.scss';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { useQuery } from 'react-query';
import { api } from '../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { useTranslation } from 'react-i18next';

interface UniverseBackupProps {
  params: {
    uuid: string;
    tab: string;
  };
}

const isPITRSupported = (version: string): boolean => {
  //PITR is supported from 2.14
  const PITR_THRESHOLD_VERSION = '2.14.0.0';
  return (
    compareYBSoftwareVersions({
      versionA: version,
      versionB: PITR_THRESHOLD_VERSION,
      options: {
        suppressFormatError: true
      }
    }) >= 0
  );
};

const UniverseBackup: FC<UniverseBackupProps> = ({ params: { uuid } }) => {
  const { t } = useTranslation();
  const [showAdvancedRestore, setShowAdvancedRestore] = useState(false);
  const [showThrottleParametersModal, setShowThrottleParametersModal] = useState(false);

  const featureFlags = useSelector((state: any) => state.featureFlags);
  // const currentUniverse = useSelector((reduxState: any) => reduxState.universe.currentUniverse);
  const currentUniverse = useQuery<Universe>(['universe', uuid], () => api.fetchUniverse(uuid));

  if (currentUniverse.isLoading || currentUniverse.isIdle) {
    return <YBLoading />;
  }

  if (currentUniverse.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('error.failedToFetchUniverse', { universeUuid: uuid })}
      />
    );
  }

  const primaryCluster = getPrimaryCluster(currentUniverse?.data?.universeDetails.clusters);
  const currentSoftwareVersion = primaryCluster.userIntent.ybSoftwareVersion.split('-')[0];

  //PITR
  const enablePITR =
    isPITRSupported(currentSoftwareVersion) &&
    (featureFlags.test.enablePITR || featureFlags.released.enablePITR);

  const YBCInstalled =
    (featureFlags.test.enableYbc || featureFlags.released.enableYbc) &&
    isYbcInstalledInUniverse(currentUniverse.data.universeDetails);

  const allowedTasks = currentUniverse?.data?.allowedTasks;

  return (
    <>
      <BackupAndRestoreBanner />
      {featureFlags.test.enableNewAdvancedRestoreModal ? (
        showAdvancedRestore && (
          <AdvancedRestoreNewModal
            onHide={() => {
              setShowAdvancedRestore(false);
            }}
            visible={showAdvancedRestore}
            currentUniverseUUID={uuid}
          />
        )
      ) : (
        <BackupAdvancedRestore
          onHide={() => {
            setShowAdvancedRestore(false);
          }}
          allowedTasks={allowedTasks}
          visible={showAdvancedRestore}
          currentUniverseUUID={uuid}
        />
      )}
      {YBCInstalled && (
        <BackupThrottleParameters
          visible={showThrottleParametersModal}
          onHide={() => setShowThrottleParametersModal(false)}
          currentUniverseUUID={uuid}
        />
      )}
      <RbacValidator
        accessRequiredOn={{
          ...ApiPermissionMap.GET_BACKUPS_BY_PAGE,
          onResource: uuid
        }}
      >
        <YBTabsPanel id="backup-tab-panel" defaultTab="backupList">
          <Tab eventKey="backupList" title="Backups" unmountOnExit>
            <BackupList allowTakingBackup universeUUID={uuid} allowedTasks={allowedTasks} />
          </Tab>
          <Tab eventKey="backupSchedule" title="Scheduled Backup Policies" unmountOnExit>
            <ScheduledBackup universeUUID={uuid} allowedTasks={allowedTasks} />
          </Tab>
          {enablePITR && (
            <Tab eventKey="point-in-time-recovery" title="Point-in-time Recovery" unmountOnExit>
              <PointInTimeRecovery universeUUID={uuid} allowedTasks={allowedTasks} />
            </Tab>
          )}
          {(featureFlags.test.enableRestore || featureFlags.released.enableRestore) && (
            <Tab eventKey="restore" title="Restore History" unmountOnExit>
              <Restore type="UNIVERSE_LEVEL" universeUUID={uuid} />
            </Tab>
          )}
          <Tab
            tabClassName="advanced_configs"
            title={
              <DropdownButton
                pullRight
                onClick={(e) => {
                  e.preventDefault();
                  e.stopPropagation();
                }}
                title={
                  <span>
                    <i className="fa fa-gear" />
                    Advanced
                  </span>
                }
                id="advanced_config_but"
              >
                <RbacValidator
                  isControl
                  accessRequiredOn={{
                    onResource: uuid,
                    ...ApiPermissionMap.RESTORE_BACKUP
                  }}
                >
                  <MenuItem
                    onClick={(e) => {
                      e.stopPropagation();
                      if (currentUniverse?.data?.universeDetails?.universePaused) return;
                      setShowAdvancedRestore(true);
                    }}
                    disabled={currentUniverse?.data?.universeDetails?.universePaused}
                    data-testid="UniverseBackup-AdvancedRestore"
                  >
                    Advanced Restore
                  </MenuItem>
                </RbacValidator>
                {YBCInstalled && (
                  <RbacValidator
                    isControl
                    accessRequiredOn={{
                      onResource: uuid,
                      ...ApiPermissionMap.MODIFY_BACKUP_THROTTLE_PARAMS
                    }}
                  >
                    <MenuItem
                      onClick={(e) => {
                        e.stopPropagation();
                        if (currentUniverse?.data?.universeDetails?.universePaused) return;
                        setShowThrottleParametersModal(true);
                      }}
                      disabled={currentUniverse?.data?.universeDetails?.universePaused}
                      data-testid="UniverseBackup-ConfigureThrottle"
                    >
                      Configure Throttle Parameters
                    </MenuItem>
                  </RbacValidator>
                )}
              </DropdownButton>
            }
          ></Tab>
        </YBTabsPanel>
      </RbacValidator>
    </>
  );
};

export const UniverseLevelBackup = withRouter(UniverseBackup);
