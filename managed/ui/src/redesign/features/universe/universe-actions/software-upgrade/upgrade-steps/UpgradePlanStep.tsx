import React, { useState } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { DragDropContext, Draggable, Droppable, DropResult } from 'react-beautiful-dnd';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';

import { PauseSlot } from './components/PauseSlot';
import type { AzUpgradeStep, DBUpgradeFormFields } from '../types';
import { AzClusterKind, DropReason } from '../constants';
import { applySlotSwap, getSlotId, parseSlotIndex } from '../utils/upgradeOrderUtils';

import DragHandleIcon from '@app/redesign/assets/draggable.svg';
import { YBTag } from '@yugabyte-ui-library/core';

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
  azSlotsColumn: {
    display: 'flex',
    flexDirection: 'column'
  },
  azSlot: {
    boxSizing: 'border-box',
    width: '100%',

    borderRadius: theme.shape.borderRadius,
    backgroundColor: 'transparent'
  },
  azSlotDragging: {
    outline: `1px solid ${theme.palette.grey[300]}`
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
  },
  // Collapse react-beautiful-dnd droppable placeholder when this slot is not the drag source so it doesn't add gap.
  placeholderWrapperCollapsed: {
    minHeight: 0,
    height: 0,
    overflow: 'hidden'
  },
  reminderContainer: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    padding: theme.spacing(0, 1)
  }
}));

export const UpgradePlanStep = () => {
  const [isPrimaryClusterDragging, setIsPrimaryClusterDragging] = useState(false);
  const [isReadReplicaClusterDragging, setIsReadReplicaClusterDragging] = useState(false);
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.upgradeModal.upgradePlanStep'
  });
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

  const createDragEndHandler = (cluster: AzClusterKind) => (dropResult: DropResult) => {
    const { source, destination, reason: dropReason } = dropResult;
    if (
      dropReason !== DropReason.DROP ||
      !destination ||
      source.droppableId === destination.droppableId
    ) {
      return;
    }

    const sourceIndex = parseSlotIndex(source.droppableId);
    const destIndex = parseSlotIndex(destination.droppableId);
    if (sourceIndex === null || destIndex === null) {
      return;
    }

    const currentCanaryUpgradeConfig = formMethods.getValues('canaryUpgradeConfig');
    if (!currentCanaryUpgradeConfig) {
      return;
    }

    const azOrderKey =
      cluster === AzClusterKind.PRIMARY ? 'primaryClusterAzOrder' : 'readReplicaClusterAzOrder';
    const azStepsKey =
      cluster === AzClusterKind.PRIMARY ? 'primaryClusterAzSteps' : 'readReplicaClusterAzSteps';
    const azOrder = currentCanaryUpgradeConfig[azOrderKey] ?? [];
    const azSteps = (currentCanaryUpgradeConfig[azStepsKey] ?? {}) as Record<string, AzUpgradeStep>;
    const { newAzOrder, newAzSteps } = applySlotSwap(azOrder, azSteps, sourceIndex, destIndex);
    formMethods.setValue('canaryUpgradeConfig', {
      ...currentCanaryUpgradeConfig,
      [azOrderKey]: newAzOrder,
      [azStepsKey]: newAzSteps
    });
  };

  const handlePrimaryDragEnd = createDragEndHandler(AzClusterKind.PRIMARY);
  const handleReadReplicaDragEnd = createDragEndHandler(AzClusterKind.READ_REPLICA);

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

        <PauseSlot
          isPaused={pauseAfterMasters}
          onToggle={() => setPauseAfterMasters(!pauseAfterMasters)}
          isBetweenStages
          testIdSuffix="afterMasters"
        />

        {/* Stage 2: Upgrade Primary Cluster T-Servers */}
        <DragDropContext
          onDragStart={() => setIsPrimaryClusterDragging(true)}
          onDragEnd={(dropResult) => {
            setIsPrimaryClusterDragging(false);
            handlePrimaryDragEnd(dropResult);
          }}
        >
          <div className={classes.upgradeStageContainer}>
            <div className={classes.upgradeStageHeader}>
              <div className={classes.iconContainer}>
                <Typography variant="subtitle1">2</Typography>
              </div>
              <Typography variant="body1">
                {hasReadReplica
                  ? t('upgradeStage.upgradePrimaryClusterTServers')
                  : t('upgradeStage.upgradeTservers')}
              </Typography>
            </div>
            <div className={classes.executionOrderContainer}>
              <Typography variant="subtitle1" className={classes.executionOrderText}>
                {t('executionOrder')}
              </Typography>
              <div className={classes.azSlotsColumn}>
                {primaryAzSteps.map((azStep, index) => (
                  <React.Fragment key={azStep.azUuid}>
                    <Droppable droppableId={getSlotId(index)} type={AzClusterKind.PRIMARY}>
                      {(droppableProvided, droppableSnapshot) => {
                        const isSourceSlot = droppableSnapshot.draggingFromThisWith;
                        return (
                          <div
                            ref={droppableProvided.innerRef}
                            {...droppableProvided.droppableProps}
                            className={clsx(
                              classes.azSlot,
                              isSourceSlot && isPrimaryClusterDragging && classes.azSlotDragging
                            )}
                          >
                            <Draggable draggableId={`primary-${azStep.azUuid}`} index={0}>
                              {(draggableProvided, draggableSnapshot) => (
                                <div
                                  ref={draggableProvided.innerRef}
                                  {...draggableProvided.draggableProps}
                                  {...draggableProvided.dragHandleProps}
                                  className={classes.azContainer}
                                  style={{
                                    ...draggableProvided.draggableProps.style,
                                    ...(draggableSnapshot.isDragging ? {} : { transform: 'none' })
                                  }}
                                >
                                  <DragHandleIcon />
                                  <Typography className={classes.bodyText}>
                                    {azStep.displayName}
                                  </Typography>
                                </div>
                              )}
                            </Draggable>
                            <div
                              className={
                                !isSourceSlot ? classes.placeholderWrapperCollapsed : undefined
                              }
                            >
                              {droppableProvided.placeholder}
                            </div>
                          </div>
                        );
                      }}
                    </Droppable>
                    {index < primaryAzSteps.length - 1 && (
                      <PauseSlot
                        isPaused={azStep.pauseAfterTserverUpgrade}
                        onToggle={() => togglePrimaryPause(azStep.azUuid)}
                        isRecommended={index === 0}
                        testIdSuffix={`primary-${index}`}
                      />
                    )}
                  </React.Fragment>
                ))}
              </div>
            </div>
          </div>
        </DragDropContext>

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
          <DragDropContext
            onDragStart={() => setIsReadReplicaClusterDragging(true)}
            onDragEnd={(dropResult) => {
              setIsReadReplicaClusterDragging(false);
              handleReadReplicaDragEnd(dropResult);
            }}
          >
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
                <div className={classes.azSlotsColumn}>
                  {readReplicaAzSteps.map((azStep, index) => (
                    <React.Fragment key={azStep.azUuid}>
                      <Droppable droppableId={getSlotId(index)} type={AzClusterKind.READ_REPLICA}>
                        {(droppableProvided, droppableSnapshot) => {
                          const isSourceSlot = droppableSnapshot.draggingFromThisWith;
                          return (
                            <div
                              ref={droppableProvided.innerRef}
                              {...droppableProvided.droppableProps}
                              className={clsx(
                                classes.azSlot,
                                isReadReplicaClusterDragging && classes.azSlotDragging
                              )}
                            >
                              <Draggable draggableId={`read-replica-${azStep.azUuid}`} index={0}>
                                {(draggableProvided, draggableSnapshot) => (
                                  <div
                                    ref={draggableProvided.innerRef}
                                    {...draggableProvided.draggableProps}
                                    {...draggableProvided.dragHandleProps}
                                    className={classes.azContainer}
                                    style={{
                                      ...draggableProvided.draggableProps.style,
                                      ...(draggableSnapshot.isDragging ? {} : { transform: 'none' })
                                    }}
                                  >
                                    <DragHandleIcon />
                                    <Typography className={classes.bodyText}>
                                      {azStep.displayName}
                                    </Typography>
                                  </div>
                                )}
                              </Draggable>
                              <div
                                className={
                                  !isSourceSlot ? classes.placeholderWrapperCollapsed : undefined
                                }
                              >
                                {droppableProvided.placeholder}
                              </div>
                            </div>
                          );
                        }}
                      </Droppable>
                      {index < readReplicaAzSteps.length - 1 && (
                        <PauseSlot
                          isPaused={azStep.pauseAfterTserverUpgrade}
                          onToggle={() => toggleReadReplicaPause(azStep.azUuid)}
                          testIdSuffix={`readReplica-${index}`}
                        />
                      )}
                    </React.Fragment>
                  ))}
                </div>
              </div>
            </div>
          </DragDropContext>
        )}
      </div>
      <div className={classes.reminderContainer}>
        <YBTag color="warning" size="medium">
          {t('reminder')}
        </YBTag>
        <Typography variant="body2">{t('pauseRequiresManualResume')}</Typography>
      </div>
    </div>
  );
};
