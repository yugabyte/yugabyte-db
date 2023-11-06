import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { DrParticipantCard } from './DrParticipantCard';
import { ReactComponent as InfoIcon } from '../../../../redesign/assets/info-message.svg';
import { ReactComponent as RightArrow } from '../../../../redesign/assets/arrow-right.svg';
import { XClusterConfigStatusLabel } from '../../XClusterConfigStatusLabel';
import { YBTooltip } from '../../../../redesign/components';

import { DrConfig } from '../types';

interface DrConfigOverviewProps {
  drConfig: DrConfig;
}

const useStyles = makeStyles((theme) => ({
  propertyLabel: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',

    width: '200px',
    minWidth: '200px'
  },
  infoIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config';

export const DrConfigOverview = ({ drConfig }: DrConfigOverviewProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const theme = useTheme();

  const {
    sourceUniverseUUID: sourceUniverseUuid,
    targetUniverseUUID: targetUniverseUuid
  } = drConfig.xClusterConfig;
  return (
    <div>
      <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
        <Box display="flex">
          <div className={classes.propertyLabel}>
            <Typography variant="body2">{t('property.drStatus.label')}</Typography>
            <YBTooltip
              title={<Typography variant="body2">{t('property.drStatus.tooltip')}</Typography>}
            >
              <InfoIcon className={classes.infoIcon} />
            </YBTooltip>
          </div>
          <XClusterConfigStatusLabel xClusterConfig={drConfig.xClusterConfig} />
        </Box>
        <Box display="flex">
          <div className={classes.propertyLabel}>
            <Typography variant="body2">{t('property.estimatedDataLoss.label')}</Typography>
            <YBTooltip
              title={
                <Typography variant="body2">{t('property.estimatedDataLoss.tooltip')}</Typography>
              }
            >
              <InfoIcon className={classes.infoIcon} />
            </YBTooltip>
          </div>
          <div>Placeholder ms</div>
        </Box>
      </Box>
      <Box
        display="flex"
        flexWrap="wrap"
        alignItems="center"
        gridGap={theme.spacing(5)}
        marginTop={3}
      >
        <div>
          <Typography variant="subtitle1">{t('participant.drPrimary')}</Typography>
          <DrParticipantCard universeUuid={sourceUniverseUuid} />
        </div>
        <RightArrow />
        <div>
          <Typography variant="subtitle1">{t('participant.drReplica')}</Typography>
          <DrParticipantCard universeUuid={targetUniverseUuid} />
        </div>
      </Box>
    </div>
  );
};
