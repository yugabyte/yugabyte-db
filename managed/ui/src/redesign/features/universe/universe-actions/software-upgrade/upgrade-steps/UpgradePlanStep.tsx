import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';

import { PauseSlot } from './components/PauseSlot';
import type { DBUpgradeFormFields } from '../types';

import DragHandleIcon from '@app/redesign/assets/draggable.svg';

const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal.upgradePlanStep';

const useStyles = makeStyles((theme) => ({
  stepContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3),

    width: '100%',

    padding: theme.spacing(3)
  },
  stepHeader: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  upgradeStages: {
    display: 'flex',
    flexDirection: 'column'
  },
  upgradeStageContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3),

    padding: theme.spacing(1.5, 2),

    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.grey[300]}`,
    backgroundColor: theme.palette.ybacolors.grey005
  },
  iconContainer: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',

    height: 32,
    width: 32,

    borderRadius: '50%',
    backgroundColor: theme.palette.grey[100]
  },
  upgradeStageHeader: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(2)
  },
  executionOrderContainer: {
    display: 'flex',
    flexDirection: 'column',

    padding: theme.spacing(0, 4, 0, 6)
  },
  executionOrderText: {
    fontWeight: 500,
    fontSize: '11.5px',
    lineHeight: '16px',
    textTransform: 'uppercase',

    marginBottom: theme.spacing(2),

    color: theme.palette.grey[600]
  },
  azContainer: {
    display: 'flex',
    gap: theme.spacing(2),
    alignItems: 'center',

    width: '100%',
    padding: theme.spacing(1, 2),

    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.grey[300]}`,
    backgroundColor: theme.palette.common.white
  },
  bodyText: {
    fontSize: '13px',
    lineHeight: '16px',
    fontWeight: 500,
    color: theme.palette.grey[900]
  }
}));

export const UpgradePlanStep = () => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const formMethods = useFormContext<DBUpgradeFormFields>();
  const canaryConfig = formMethods.watch('canaryUpgradeConfig');

  const primaryAzSteps = (canaryConfig?.primaryClusterAzOrder ?? [])
    .map((azUuid) => canaryConfig?.primaryClusterAzSteps?.[azUuid])
    .filter((step): step is NonNullable<typeof step> => step !== null && step !== undefined);
  const readReplicaAzSteps = (canaryConfig?.readReplicaClusterAzOrder ?? [])
    .map((azUuid) => canaryConfig?.readReplicaClusterAzSteps?.[azUuid])
    .filter((step): step is NonNullable<typeof step> => step !== null && step !== undefined);

  const lastPrimaryIndex = primaryAzSteps.length - 1;
  const pauseBeforeReadReplica =
    lastPrimaryIndex >= 0 ? primaryAzSteps[lastPrimaryIndex].pauseAfterTserverUpgrade : false;

  const pauseAfterMasters = canaryConfig?.pauseAfterMasters ?? true;

  const setPauseAfterMasters = (value: boolean) => {
    const currentCanaryUpgradeConfig = formMethods.getValues('canaryUpgradeConfig');
    if (!currentCanaryUpgradeConfig) {
      return;
    }
    formMethods.setValue('canaryUpgradeConfig', {
      ...currentCanaryUpgradeConfig,
      pauseAfterMasters: value
    });
  };

  const togglePrimaryPause = (azUuid: string) => {
    const currentCanaryUpgradeConfig = formMethods.getValues('canaryUpgradeConfig');
    const azUpgradeSteps = currentCanaryUpgradeConfig?.primaryClusterAzSteps;
    if (!azUpgradeSteps) {
      return;
    }
    const azUpgradeStep = azUpgradeSteps[azUuid];
    if (!azUpgradeStep) {
      return;
    }
    formMethods.setValue('canaryUpgradeConfig', {
      ...currentCanaryUpgradeConfig!,
      primaryClusterAzSteps: {
        ...azUpgradeSteps,
        [azUuid]: {
          ...azUpgradeStep,
          pauseAfterTserverUpgrade: !azUpgradeStep.pauseAfterTserverUpgrade
        }
      }
    });
  };

  const toggleReadReplicaPause = (azUuid: string) => {
    const currentCanaryUpgradeConfig = formMethods.getValues('canaryUpgradeConfig');
    const azUpgradeSteps = currentCanaryUpgradeConfig?.readReplicaClusterAzSteps;
    if (!azUpgradeSteps) {
      return;
    }
    const azUpgradeStep = azUpgradeSteps[azUuid];
    if (!azUpgradeStep) {
      return;
    }
    formMethods.setValue('canaryUpgradeConfig', {
      ...currentCanaryUpgradeConfig!,
      readReplicaClusterAzSteps: {
        ...azUpgradeSteps,
        [azUuid]: {
          ...azUpgradeStep,
          pauseAfterTserverUpgrade: !azUpgradeStep.pauseAfterTserverUpgrade
        }
      }
    });
  };

  const hasReadReplica = readReplicaAzSteps.length > 0;

  return (
    <div className={classes.stepContainer}>
      <div className={classes.stepHeader}>
        <Typography variant="h5">{t('stepTitle')}</Typography>
        <Typography variant="subtitle1">{t('stepDescription')}</Typography>
      </div>
      <div className={classes.upgradeStages}>
        {/* Stage 1: Upgrade Master Servers */}
        <div className={classes.upgradeStageContainer}>
          <div className={classes.upgradeStageHeader}>
            <div className={classes.iconContainer}>
              <Typography variant="subtitle1">1</Typography>
            </div>
            <Typography variant="body1">{t('upgradeStage.upgradeMasterServers')}</Typography>
          </div>
        </div>

        {/* Pause after masters (between stage 1 and 2) */}
        <PauseSlot
          isPaused={pauseAfterMasters}
          onToggle={() => setPauseAfterMasters(!pauseAfterMasters)}
          isBetweenStages
          testIdSuffix="afterMasters"
        />

        {/* Stage 2: Upgrade Primary Cluster T-Servers */}
        <div className={classes.upgradeStageContainer}>
          <div className={classes.upgradeStageHeader}>
            <div className={classes.iconContainer}>
              <Typography variant="subtitle1">2</Typography>
            </div>
            <Typography variant="body1">
              {t('upgradeStage.upgradePrimaryClusterTServers')}
            </Typography>
          </div>
          <div className={classes.executionOrderContainer}>
            <Typography variant="subtitle1" className={classes.executionOrderText}>
              {t('executionOrder')}
            </Typography>
            {primaryAzSteps.map((azStep, index) => (
              <div key={azStep.azUuid}>
                <div className={classes.azContainer}>
                  <DragHandleIcon />
                  <Typography className={classes.bodyText}>{azStep.displayName}</Typography>
                </div>
                {index < primaryAzSteps.length - 1 && (
                  <PauseSlot
                    isPaused={azStep.pauseAfterTserverUpgrade}
                    onToggle={() => togglePrimaryPause(azStep.azUuid)}
                    isRecommended={index === 0}
                    testIdSuffix={`primary-${index}`}
                  />
                )}
              </div>
            ))}
          </div>
        </div>

        {/* Pause before read replica (between stage 2 and 3) */}
        {hasReadReplica && (
          <PauseSlot
            isPaused={pauseBeforeReadReplica}
            onToggle={() => togglePrimaryPause(primaryAzSteps[lastPrimaryIndex].azUuid)}
            isBetweenStages
            testIdSuffix="beforeReadReplica"
          />
        )}

        {/* Stage 3: Upgrade Read Replica T-Servers */}
        {hasReadReplica && (
          <div className={classes.upgradeStageContainer}>
            <div className={classes.upgradeStageHeader}>
              <div className={classes.iconContainer}>
                <Typography variant="subtitle1">3</Typography>
              </div>
              <Typography variant="body1">
                {t('upgradeStage.upgradeReadReplicaTServers')}
              </Typography>
            </div>
            <div className={classes.executionOrderContainer}>
              <Typography variant="subtitle1" className={classes.executionOrderText}>
                {t('executionOrder')}
              </Typography>
              {readReplicaAzSteps.map((azStep, index) => (
                <div key={azStep.azUuid}>
                  <div className={classes.azContainer}>
                    <DragHandleIcon />
                    <Typography className={classes.bodyText}>{azStep.displayName}</Typography>
                  </div>
                  {index < readReplicaAzSteps.length - 1 && (
                    <PauseSlot
                      isPaused={azStep.pauseAfterTserverUpgrade}
                      onToggle={() => toggleReadReplicaPause(azStep.azUuid)}
                      testIdSuffix={`readReplica-${index}`}
                    />
                  )}
                </div>
              ))}
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
