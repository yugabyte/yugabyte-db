import { FC } from 'react';
import { Typography } from '@material-ui/core';
import { IconPosition, StatusType, YBSmartStatus, YBTooltip } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';

import { UniverseInfoSoftwareUpgradeState } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { Task, TaskState } from '../../tasks/dtos';
import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';

import { dbVersionWidgetStyles } from './DBVersionWidgetStyles';
import {
  getIsDbUpgradeFinalizeTask,
  getIsDbUpgradeRollbackTask,
  getIsDbUpgradeTask
} from '../../tasks/TaskUtils';
import { formatYbSoftwareVersionString } from '@app/utils/Formatters';

interface DbVersionWidgetTagProps {
  latestSoftwareUpgradeLockingTask: Task | undefined;
  softwareUpgradeState: UniverseInfoSoftwareUpgradeState | undefined;
}

export const DbVersionWidgetTag: FC<DbVersionWidgetTagProps> = ({
  latestSoftwareUpgradeLockingTask,
  softwareUpgradeState
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.clusterWidget'
  });
  const classes = dbVersionWidgetStyles();

  if (!softwareUpgradeState || !latestSoftwareUpgradeLockingTask) {
    return null;
  }

  if (getIsDbUpgradeTask(latestSoftwareUpgradeLockingTask)) {
    const targetDbVersionLabel = formatYbSoftwareVersionString(
      latestSoftwareUpgradeLockingTask?.details?.versionNumbers?.ybSoftwareVersion ?? ''
    );
    switch (latestSoftwareUpgradeLockingTask.status) {
      case TaskState.RUNNING:
      case TaskState.ABORT:
        return (
          <div className={classes.upgradeStateContainer}>
            <Typography variant="body1" className={classes.text}>
              {t('upgradingTo', { version: targetDbVersionLabel })}
            </Typography>
            <YBSmartStatus type={StatusType.IN_PROGRESS} label={t('tag.operationInProgress')} />
          </div>
        );
      case TaskState.PAUSED:
        return (
          <div className={classes.upgradeStateContainer}>
            <Typography variant="body1" className={classes.text}>
              {t('upgradingTo', { version: targetDbVersionLabel })}
            </Typography>
            <YBSmartStatus
              type={StatusType.PENDING}
              label={t('tag.operationPaused')}
              iconPosition={IconPosition.NONE}
            />
          </div>
        );
      case TaskState.SUCCESS:
        if (softwareUpgradeState === UniverseInfoSoftwareUpgradeState.PreFinalize) {
          return (
            <YBTooltip title={t('tooltip.upgradeNotCompleteUntilFinalized')}>
              <span>
                <YBSmartStatus type={StatusType.PENDING} label={t('tag.pendingFinalization')} />
              </span>
            </YBTooltip>
          );
        }
        return null;
      case TaskState.FAILURE:
      case TaskState.ABORTED:
        if (softwareUpgradeState === UniverseInfoSoftwareUpgradeState.Ready) {
          // DB upgrade failed during pre-check stage.
          return null;
        }

        return (
          <div className={classes.upgradeStateContainer}>
            <Typography variant="body1" className={classes.text}>
              {t('upgradingTo', { version: targetDbVersionLabel })}
            </Typography>
            <YBSmartStatus type={StatusType.ERROR} label={t('tag.operationFailed')} />
          </div>
        );
      case TaskState.CREATED:
      case TaskState.INITIALIZING:
      case TaskState.UNKNOWN:
        return null;
      default:
        return assertUnreachableCase(latestSoftwareUpgradeLockingTask.status);
    }
  }

  if (getIsDbUpgradeRollbackTask(latestSoftwareUpgradeLockingTask)) {
    const previousDbVersionLabel = formatYbSoftwareVersionString(
      latestSoftwareUpgradeLockingTask.details?.versionNumbers?.ybSoftwareVersion ?? ''
    );
    switch (latestSoftwareUpgradeLockingTask.status) {
      case TaskState.RUNNING:
      case TaskState.ABORT:
        return (
          <div className={classes.upgradeStateContainer}>
            <Typography variant="body1" className={classes.text}>
              {t('rollingBackTo', { version: previousDbVersionLabel })}
            </Typography>
            <YBSmartStatus type={StatusType.IN_PROGRESS} label={t('tag.operationInProgress')} />
          </div>
        );
      case TaskState.FAILURE:
      case TaskState.ABORTED:
        return (
          <div className={classes.upgradeStateContainer}>
            <Typography variant="body1" className={classes.text}>
              {t('rollingBackTo', { version: previousDbVersionLabel })}
            </Typography>
            <YBSmartStatus type={StatusType.ERROR} label={t('tag.operationFailed')} />
          </div>
        );
      case TaskState.SUCCESS:
      case TaskState.PAUSED:
      case TaskState.CREATED:
      case TaskState.INITIALIZING:
      case TaskState.UNKNOWN:
        return null;
      default:
        return assertUnreachableCase(latestSoftwareUpgradeLockingTask.status);
    }
  }

  if (getIsDbUpgradeFinalizeTask(latestSoftwareUpgradeLockingTask)) {
    // Although not intuitive, the finalize DB upgrade task stores the intended final
    // DB version in the `ybPrevSoftwareVersion` field.
    const finalizeDbVersionLabel = formatYbSoftwareVersionString(
      latestSoftwareUpgradeLockingTask.details?.versionNumbers?.ybPrevSoftwareVersion ?? ''
    );
    switch (latestSoftwareUpgradeLockingTask.status) {
      case TaskState.RUNNING:
      case TaskState.ABORT:
        return (
          <div className={classes.upgradeStateContainer}>
            <Typography variant="body1" className={classes.text}>
              {t('finalizingUpgrade', { version: finalizeDbVersionLabel })}
            </Typography>
            <YBSmartStatus type={StatusType.IN_PROGRESS} label={t('tag.operationInProgress')} />
          </div>
        );
      case TaskState.FAILURE:
      case TaskState.ABORTED:
        return (
          <div className={classes.upgradeStateContainer}>
            <Typography variant="body1" className={classes.text}>
              {t('finalizingUpgrade', { version: finalizeDbVersionLabel })}
            </Typography>
            <YBTooltip title={t('tooltip.upgradeNotCompleteUntilFinalized')}>
              <span>
                <YBSmartStatus type={StatusType.ERROR} label={t('tag.operationFailed')} />
              </span>
            </YBTooltip>
          </div>
        );
      case TaskState.SUCCESS:
      case TaskState.PAUSED:
      case TaskState.CREATED:
      case TaskState.INITIALIZING:
      case TaskState.UNKNOWN:
        return null;
      default:
        return assertUnreachableCase(latestSoftwareUpgradeLockingTask.status);
    }
  }

  return null;
};
