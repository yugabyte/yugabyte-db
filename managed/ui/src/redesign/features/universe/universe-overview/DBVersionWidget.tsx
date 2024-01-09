import { FC, useState, useEffect, useMemo, useCallback } from 'react';
import _ from 'lodash';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { Box, Typography, Link } from '@material-ui/core';
import { YBTooltip } from '../../../components';
import { YBWidget } from '../../../../components/panels';
import clsx from 'clsx';
import { YBLoadingCircleIcon } from '../../../../components/common/indicators';
import { DBUpgradeModal } from '../universe-actions/rollback-upgrade/DBUpgradeModal';
import { isNonEmptyObject } from '../../../../utils/ObjectUtils';
import {
  getUniverseStatus,
  SoftwareUpgradeState,
  getUniversePendingTask,
  UniverseState,
  SoftwareUpgradeTaskType
} from '../../../../components/universes/helpers/universeHelpers';
import { dbVersionWidgetStyles } from './DBVersionWidgetStyles';
import { getPrimaryCluster } from '../../../../utils/UniverseUtils';
import { TaskObject } from '../universe-actions/rollback-upgrade/utils/types';
//icons
import UpgradeArrow from '../../../assets/upgrade-arrow.svg';
import WarningExclamation from '../../../assets/warning-triangle.svg';

interface DBVersionWidgetProps {
  higherVersionCount: number;
  isRollBackFeatureEnabled: boolean;
  failedTaskDetails: TaskObject;
}

export const DBVersionWidget: FC<DBVersionWidgetProps> = ({
  higherVersionCount,
  isRollBackFeatureEnabled,
  failedTaskDetails
}) => {
  const { t } = useTranslation();
  const classes = dbVersionWidgetStyles();
  const currentUniverse = useSelector((state: any) => state.universe.currentUniverse.data);
  const tasks = useSelector((state: any) => state.tasks);
  const [openUpgradeModal, setUpgradeModal] = useState(false);
  const primaryCluster = getPrimaryCluster(currentUniverse?.universeDetails?.clusters);
  const dbVersionValue = primaryCluster?.userIntent?.ybSoftwareVersion;
  const minifiedCurrentVersion = dbVersionValue?.split('-')[0];
  const upgradeState = currentUniverse?.universeDetails?.softwareUpgradeState;
  const previousDBVersion = currentUniverse?.universeDetails?.prevYBSoftwareConfig?.softwareVersion;
  const isUniversePaused = currentUniverse?.universeDetails?.universePaused;
  const universeStatus = getUniverseStatus(currentUniverse);
  const universePendingTask = getUniversePendingTask(
    currentUniverse?.universeUUID,
    tasks?.customerTaskList
  );
  const failedTaskTargetVersion = failedTaskDetails?.details?.versionNumbers?.ybSoftwareVersion;

  const upgradingVersion = universePendingTask?.details?.versionNumbers?.ybSoftwareVersion;

  let statusDisplay = (
    <Box display="flex" flexDirection={'row'} alignItems={'center'}>
      <Typography className={classes.versionText}>v{minifiedCurrentVersion}</Typography>
      &nbsp;
      {isRollBackFeatureEnabled &&
        higherVersionCount > 0 &&
        upgradeState === SoftwareUpgradeState.READY &&
        !isUniversePaused &&
        _.isEmpty(universePendingTask) && (
          <>
            <img src={UpgradeArrow} height="14px" width="14px" alt="--" /> &nbsp;
            <Link
              component={'button'}
              underline="always"
              onClick={() => setUpgradeModal(true)}
              className={classes.upgradeLink}
            >
              {t('universeActions.dbRollbackUpgrade.widget.upgradeAvailable')}
            </Link>
          </>
        )}
      {upgradeState === SoftwareUpgradeState.PRE_FINALIZE && (
        <YBTooltip title="Pending upgrade Finalization">
          <img src={WarningExclamation} height={'14px'} width="14px" alt="--" />
        </YBTooltip>
      )}
      {[SoftwareUpgradeState.FINALIZE_FAILED, SoftwareUpgradeState.UPGRADE_FAILED].includes(
        upgradeState
      ) && (
        <YBTooltip
          title={
            upgradeState === SoftwareUpgradeState.FINALIZE_FAILED
              ? `Failed to finalize upgrade to v${failedTaskTargetVersion}`
              : `Failed to upgrade database version to v${failedTaskTargetVersion}`
          }
        >
          <span>
            <i className={`fa fa-warning ${classes.errorIcon}`} />
          </span>
        </YBTooltip>
      )}
      {upgradeState === SoftwareUpgradeState.ROLLBACK_FAILED && (
        <YBTooltip title={`Failed to rollback to v${failedTaskTargetVersion}`}>
          <span>
            <i className={`fa fa-warning ${classes.errorIcon}`} />
          </span>
        </YBTooltip>
      )}
    </Box>
  );

  if (
    universeStatus.state === UniverseState.PENDING &&
    isNonEmptyObject(universePendingTask) &&
    upgradeState !== SoftwareUpgradeState.READY
  ) {
    statusDisplay = (
      <Box display={'flex'} flexDirection={'row'} alignItems="baseline">
        <YBLoadingCircleIcon size="inline" variant="primary" />
        <Typography variant="body2" className={classes.blueText}>
          {universePendingTask.type === SoftwareUpgradeTaskType.ROLLBACK_UPGRADE &&
            (t('universeActions.dbRollbackUpgrade.widget.rollingBackTooltip', {
              version: previousDBVersion
            }) as string)}
          {universePendingTask.type === SoftwareUpgradeTaskType.FINALIZE_UPGRADE &&
            (t('universeActions.dbRollbackUpgrade.widget.finalizingTooltip', {
              version: minifiedCurrentVersion
            }) as string)}
          {universePendingTask.type === SoftwareUpgradeTaskType.SOFTWARE_UPGRADE &&
            (t('universeActions.dbRollbackUpgrade.widget.upgradingTooltip', {
              version: upgradingVersion
            }) as string)}
        </Typography>
      </Box>
    );
  }

  return (
    <div key="dbVersion">
      {
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
              <Typography variant="body1">
                {t('universeActions.dbRollbackUpgrade.widget.versionLabel')}
              </Typography>
              {statusDisplay}
            </Box>
          }
        />
      }
      <DBUpgradeModal
        open={openUpgradeModal}
        onClose={() => {
          setUpgradeModal(false);
        }}
        universeData={currentUniverse}
      />
    </div>
  );
};
