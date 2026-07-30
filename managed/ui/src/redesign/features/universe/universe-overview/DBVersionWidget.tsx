import { FC, useState } from 'react';
import { Box, Link, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';

import { YBWidget } from '@app/components/panels';
import {
  getUniverseStatus,
  UniverseState
} from '@app/components/universes/helpers/universeHelpers';
import { getLatestSoftwareUpgradeLockingTaskForUniverse } from '@app/redesign/features/tasks/TaskUtils';
import { DBUpgradeModal as LegacyDBUpgradeModal } from '@app/redesign/features/universe/universe-actions/rollback-upgrade/DBUpgradeModal';
import { DbUpgradeModal } from '@app/redesign/features/universe/universe-actions/software-upgrade/DbUpgradeModal';
import { Universe } from '@app/redesign/features/universe/universe-form/utils/dto';
import { universeQueryKey } from '@app/redesign/helpers/api';
import { formatYbSoftwareVersionString } from '@app/utils/Formatters';
import { getUniverse } from '@app/v2/api/universe/universe';
import { UniverseInfoSoftwareUpgradeState } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { DbVersionWidgetTag } from './DbVersionWidgetTag';
import { dbVersionWidgetStyles } from './DBVersionWidgetStyles';

import UpgradeArrow from '@app/redesign/assets/upgrade-arrow.svg?img';
import { TaskState } from '../../tasks/dtos';

interface DBVersionWidgetProps {
  universeUuid: string;
  /** Legacy universe payload for the non-canary upgrade modal only. */
  universeDataForLegacyDbUpgrade: Universe;
  higherVersionCount: number;
  isRollBackFeatureEnabled: boolean;
  isCanaryUpgradeEnabled: boolean;
}

export const DBVersionWidget: FC<DBVersionWidgetProps> = ({
  universeUuid,
  universeDataForLegacyDbUpgrade,
  higherVersionCount,
  isRollBackFeatureEnabled,
  isCanaryUpgradeEnabled
}) => {
  const [isDbUpgradeModalOpen, setIsDbUpgradeModalOpen] = useState(false);
  const classes = dbVersionWidgetStyles();
  const tasks = useSelector((state: any) => state.tasks);
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.clusterWidget'
  });
  const universeQuery = useQuery(
    universeQueryKey.detailsV2(universeUuid),
    () => getUniverse(universeUuid),
    {
      enabled: !!universeUuid
    }
  );

  const v2UniverseInfo = universeQuery.data?.info;
  const v2UniverseSpec = universeQuery.data?.spec;
  const rawYbSoftwareVersion = v2UniverseSpec?.yb_software_version ?? '';
  const dbVersionLabel = rawYbSoftwareVersion
    ? formatYbSoftwareVersionString(rawYbSoftwareVersion)
    : '';

  const softwareUpgradeState = v2UniverseInfo?.software_upgrade_state;

  const universeStatus = getUniverseStatus(
    v2UniverseInfo
      ? {
          universeDetails: {
            updateInProgress: v2UniverseInfo.update_in_progress,
            updateSucceeded: v2UniverseInfo.update_succeeded,
            universePaused: v2UniverseInfo.universe_paused,
            placementModificationTaskUuid: v2UniverseInfo.placement_modification_task_uuid,
            errorString: ''
          }
        }
      : undefined
  );
  const latestSoftwareUpgradeLockingTask = getLatestSoftwareUpgradeLockingTaskForUniverse(
    tasks?.customerTaskList,
    universeUuid
  );
  const shouldShowDbVersionLabel =
    (universeStatus.state === UniverseState.GOOD ||
      universeStatus.state === UniverseState.PAUSED ||
      universeStatus.state === UniverseState.PENDING) &&
    latestSoftwareUpgradeLockingTask?.status !== TaskState.RUNNING;
  const shouldShowUpgradeAvailableLink =
    universeStatus.state === UniverseState.GOOD &&
    latestSoftwareUpgradeLockingTask?.status !== TaskState.RUNNING &&
    isRollBackFeatureEnabled &&
    higherVersionCount > 0 &&
    softwareUpgradeState === UniverseInfoSoftwareUpgradeState.Ready;

  const statusDisplay = (
    <Box display="flex" gridGap={8} alignItems="center">
      {shouldShowDbVersionLabel && (
        <Typography variant="h4" className={classes.text}>
          {dbVersionLabel}
        </Typography>
      )}
      {shouldShowUpgradeAvailableLink && (
        <div className={classes.upgradeAvailableLinkContainer}>
          <img src={UpgradeArrow} height="14px" width="14px" alt="--" /> &nbsp;
          <Link
            component="button"
            underline="always"
            onClick={() => setIsDbUpgradeModalOpen(true)}
            className={classes.upgradeLink}
          >
            {t('upgradeAvailable')}
          </Link>
        </div>
      )}
      {universeStatus.state !== UniverseState.PAUSED && (
        <DbVersionWidgetTag
          latestSoftwareUpgradeLockingTask={latestSoftwareUpgradeLockingTask}
          softwareUpgradeState={softwareUpgradeState}
        />
      )}
    </Box>
  );

  return (
    <div key="dbVersion">
      <YBWidget
        noHeader
        noMargin
        size={1}
        className={clsx('overview-widget-database', classes.versionContainer)}
        body={
          <Box
            display={'flex'}
            flexDirection={'row'}
            pt={3}
            pl={2}
            pr={2}
            width="100%"
            height={'100%'}
            justifyContent={'space-between'}
            alignItems={'center'}
          >
            <Typography variant="body1">{t('version')}</Typography>
            {statusDisplay}
          </Box>
        }
      />
      {isDbUpgradeModalOpen &&
        (isCanaryUpgradeEnabled ? (
          <DbUpgradeModal
            universeUuid={universeUuid}
            modalProps={{
              open: isDbUpgradeModalOpen,
              onClose: () => setIsDbUpgradeModalOpen(false)
            }}
          />
        ) : (
          <LegacyDBUpgradeModal
            open={isDbUpgradeModalOpen}
            onClose={() => {
              setIsDbUpgradeModalOpen(false);
            }}
            universeData={universeDataForLegacyDbUpgrade}
          />
        ))}
    </div>
  );
};
