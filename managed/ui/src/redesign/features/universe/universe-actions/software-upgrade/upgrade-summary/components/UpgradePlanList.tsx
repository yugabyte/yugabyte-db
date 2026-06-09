import { Fragment } from 'react';
import { makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { CanaryUpgradeConfig } from '../../types';
import { ListElement, ListElementType } from './ListElement';

const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal.summaryCard';

const useStyles = makeStyles((theme) => ({
  pauseText: {
    fontSize: '10px',
    fontWeight: 500,
    lineHeight: '16px',
    color: theme.palette.grey[500]
  }
}));

interface UpgradePlanListProps {
  canaryUpgradeConfig?: CanaryUpgradeConfig | null;
}

export const UpgradePlanList = ({ canaryUpgradeConfig }: UpgradePlanListProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

  const getPrimaryClusterAzIndex = (index: number) => index + 2;
  const getReadReplicaAzIndex = (index: number) =>
    index + 2 + (canaryUpgradeConfig?.primaryClusterAzOrder.length ?? 0);

  return (
    <div>
      <ListElement
        type={ListElementType.NUMBERED}
        index={1}
        listElementContent={t('masterServers')}
      />
      {!!canaryUpgradeConfig?.pauseAfterMasters && (
        <ListElement
          type={ListElementType.BULLET}
          contentClassName={classes.pauseText}
          listElementContent={t('pause')}
        />
      )}
      {canaryUpgradeConfig &&
        canaryUpgradeConfig.primaryClusterAzOrder.map((azUuid, index) => (
          <Fragment key={azUuid}>
            <ListElement
              type={ListElementType.NUMBERED}
              index={getPrimaryClusterAzIndex(index)}
              listElementContent={
                canaryUpgradeConfig.primaryClusterAzSteps[azUuid].displayNameWithoutRegion
              }
            />
            {canaryUpgradeConfig.primaryClusterAzSteps[azUuid].pauseAfterTserverUpgrade && (
              <ListElement
                type={ListElementType.BULLET}
                contentClassName={classes.pauseText}
                listElementContent={t('pause')}
              />
            )}
          </Fragment>
        ))}
      {canaryUpgradeConfig &&
        canaryUpgradeConfig.readReplicaClusterAzOrder.map((azUuid, index) => (
          <Fragment key={azUuid}>
            <ListElement
              type={ListElementType.NUMBERED}
              index={getReadReplicaAzIndex(index)}
              listElementContent={`${canaryUpgradeConfig.readReplicaClusterAzSteps[azUuid].displayNameWithoutRegion} (${t('readReplica')})`}
            />
            {canaryUpgradeConfig.readReplicaClusterAzSteps[azUuid].pauseAfterTserverUpgrade && (
              <ListElement
                type={ListElementType.BULLET}
                contentClassName={classes.pauseText}
                listElementContent={t('pause')}
              />
            )}
          </Fragment>
        ))}
    </div>
  );
};
