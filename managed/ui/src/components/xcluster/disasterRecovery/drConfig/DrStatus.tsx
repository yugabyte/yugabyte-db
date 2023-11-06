import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { CurrentReplicationLag } from '../../ReplicationUtils';
import { XClusterConfigStatusLabel } from '../../XClusterConfigStatusLabel';
import { ReplicationTables } from '../../configDetails/ReplicationTables';

import { DrConfig } from '../types';

interface DrStatusProps {
  drConfig: DrConfig;
}

const useStyles = makeStyles((theme) => ({
  drMetadataContainer: {
    display: 'flex',
    flexWrap: 'wrap',
    gap: theme.spacing(2),

    padding: `${theme.spacing(4)}px ${theme.spacing(2)}px`,
    marginBottom: theme.spacing(3),

    background: theme.palette.common.white,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: '8px'
  },
  propertyLabel: {
    width: '200px',
    minWidth: '200px'
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config';

export const DrStatus = ({ drConfig }: DrStatusProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const classes = useStyles();

  return (
    <div>
      <div className={classes.drMetadataContainer}>
        <Box display="flex" justifyContent="space-between" width="40%">
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
            <Box display="flex" alignItems="center" gridGap={theme.spacing(2)}>
              <Typography variant="body2">{t('property.drStatus.label')}</Typography>
              <XClusterConfigStatusLabel xClusterConfig={drConfig.xClusterConfig} />
            </Box>
            <div />
          </Box>
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
            <Box display="flex">
              <Typography className={classes.propertyLabel} variant="body2">
                {t('property.replicationLag')}
              </Typography>
              <CurrentReplicationLag
                xClusterConfigUUID={drConfig.xClusterConfig.uuid}
                xClusterConfigStatus={drConfig.xClusterConfig.status}
                sourceUniverseUUID={drConfig.xClusterConfig.sourceUniverseUUID}
              />
            </Box>
            <Box display="flex">
              <Typography className={classes.propertyLabel} variant="body2">
                {t('property.minSafeTimeLagAcrossAllTables')}
              </Typography>
              {/* TODO: Replace the following placeholder when adding the relevant metrics for DR. */}
              <CurrentReplicationLag
                xClusterConfigUUID={drConfig.xClusterConfig.uuid}
                xClusterConfigStatus={drConfig.xClusterConfig.status}
                sourceUniverseUUID={drConfig.xClusterConfig.sourceUniverseUUID}
              />
            </Box>
          </Box>
        </Box>
      </div>
      <ReplicationTables xClusterConfig={drConfig.xClusterConfig} isDrConfig={true} />
    </div>
  );
};
