import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import clsx from 'clsx';

import { getLatestSchemaChangeModeSupported, getSchemaChangeMode } from '../../ReplicationUtils';
import { getXClusterConfig } from '../utils';
import { SchemaChangeModeInfoModal } from '../../sharedComponents/SchemaChangeInfoModal';
import { api, universeQueryKey } from '../../../../redesign/helpers/api';
import { getPrimaryCluster } from '../../../../utils/universeUtilsTyped';
import { ReactComponent as UpArrow } from '../../../../redesign/assets/upgrade-arrow.svg';
import { ReactComponent as QuestionCircle } from '../../../../redesign/assets/question-circle.svg';
import { UpgradeXClusterModal } from '../../sharedComponents/UpgradeXClusterModal';
import { SchemaChangesInfoPopover } from '../../sharedComponents/SchemaChangesInfoPopover';

import { DrConfig } from '../dtos';

interface SchemaChangeModeLabelProps {
  drConfig: DrConfig;
}

const useStyles = makeStyles((theme) => ({
  labelContainer: {
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center'
  },
  questionCircleIcon: {
    width: '16px',
    height: '16px'
  },
  upArrowIcon: {
    width: '14px',
    height: '14px'
  },
  schemaChangeModeInfoModalLink: {
    color: '#151730',
    cursor: 'pointer',
    '&:hover': {
      color: '#3E66FB',
      textDecoration: 'underline'
    }
  },
  upgradeAvailableLink: {
    color: theme.palette.orange[500],
    cursor: 'pointer',
    textDecoration: 'underline'
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.schemaChangeMode';
export const SchemaChangeModeLabel = ({ drConfig }: SchemaChangeModeLabelProps) => {
  const [isSchemaChangeInfoModalOpen, setIsSchemaChangeInfoModalOpen] = useState<boolean>(false);
  const [isUpgradeXClusterModalOpen, setIsUpgradeXClusterModalOpen] = useState<boolean>(false);

  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const theme = useTheme();

  const {
    primaryUniverseUuid: sourceUniverseUuid,
    drReplicaUniverseUuid: targetUniverseUuid
  } = drConfig;
  const sourceUniverseQuery = useQuery(universeQueryKey.detail(sourceUniverseUuid), () =>
    api.fetchUniverse(sourceUniverseUuid)
  );
  const targetUniverseQuery = useQuery(universeQueryKey.detail(targetUniverseUuid), () =>
    api.fetchUniverse(targetUniverseUuid)
  );

  const openSchemaChangeInfoModal = () => setIsSchemaChangeInfoModalOpen(true);
  const closeSchemaChangeInfoModal = () => setIsSchemaChangeInfoModalOpen(false);
  const openUpgradeXClusterModal = () => setIsUpgradeXClusterModalOpen(true);
  const closeUpgradeXClusterModal = () => setIsUpgradeXClusterModalOpen(false);

  const sourceUniverseClusters = sourceUniverseQuery.data?.universeDetails.clusters;
  const sourceUniverseVersion = sourceUniverseClusters
    ? getPrimaryCluster(sourceUniverseClusters)?.userIntent.ybSoftwareVersion ?? ''
    : '';
  const targetUniverseClusters = targetUniverseQuery.data?.universeDetails.clusters;
  const targetUniverseVersion = targetUniverseClusters
    ? getPrimaryCluster(targetUniverseClusters)?.userIntent.ybSoftwareVersion ?? ''
    : '';
  const xClusterConfig = getXClusterConfig(drConfig);
  const schemaChangeMode = getSchemaChangeMode(xClusterConfig);
  const latestSchemaChangeModeSupported = getLatestSchemaChangeModeSupported(
    sourceUniverseVersion,
    targetUniverseVersion
  );
  const isLatestSchemaChangeModeUsed = schemaChangeMode === latestSchemaChangeModeSupported;

  return (
    <Box display="flex" gridGap={theme.spacing(2)} alignItems="center">
      <div className={classes.labelContainer}>
        <Typography
          variant="body2"
          className={classes.schemaChangeModeInfoModalLink}
          onClick={openSchemaChangeInfoModal}
        >
          {t(`mode.${schemaChangeMode}_titleCase`)}
        </Typography>
        <SchemaChangesInfoPopover isDrInterface={true}>
          <QuestionCircle />
        </SchemaChangesInfoPopover>
      </div>
      {!isLatestSchemaChangeModeUsed && (
        <Typography
          variant="body2"
          className={clsx(classes.upgradeAvailableLink, classes.labelContainer)}
          onClick={openUpgradeXClusterModal}
        >
          <UpArrow className={classes.upArrowIcon} />
          {t('upgradeAvailable')}
        </Typography>
      )}
      {isSchemaChangeInfoModalOpen && (
        <SchemaChangeModeInfoModal
          xClusterConfig={xClusterConfig}
          sourceUniverseVersion={sourceUniverseVersion}
          targetUniverseVersion={targetUniverseVersion}
          isDrInterface={true}
          isConfigInterface={true}
          modalProps={{ open: isSchemaChangeInfoModalOpen, onClose: closeSchemaChangeInfoModal }}
        />
      )}
      {isUpgradeXClusterModalOpen && (
        <UpgradeXClusterModal
          xClusterConfig={xClusterConfig}
          isDrInterface={true}
          modalProps={{ open: isUpgradeXClusterModalOpen, onClose: closeUpgradeXClusterModal }}
        />
      )}
    </Box>
  );
};
