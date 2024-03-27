import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { DrParticipantCard } from './DrParticipantCard';
import { ReactComponent as InfoIcon } from '../../../../redesign/assets/info-message.svg';
import { YBTooltip } from '../../../../redesign/components';
import { DrConfigStateLabel } from '../DrConfigStateLabel';
import { UniverseXClusterRole } from '../../constants';
import { ReplicationIcon } from '../../icons/ReplicationIcon';
import { getXClusterConfig } from '../utils';
import { EstimatedDataLossLabel } from './EstimatedDataLossLabel';

import { DrConfig } from '../dtos';

interface DrConfigOverviewProps {
  drConfig: DrConfig;
}

const useStyles = makeStyles((theme) => ({
  overviewContainer: {
    padding: `${theme.spacing(3)}px ${theme.spacing(4)}px`,

    background: theme.palette.background.paper,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: '8px'
  },
  drParticipantContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  propertyLabel: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',

    width: '220px',
    minWidth: '100px'
  },
  infoIcon: {
    minWidth: '14px',

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

  const xClusterConfig = getXClusterConfig(drConfig);
  return (
    <div className={classes.overviewContainer}>
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
          <DrConfigStateLabel drConfig={drConfig} />
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
          <EstimatedDataLossLabel drConfigUuid={drConfig.uuid} />
        </Box>
      </Box>
      <Box
        display="flex"
        flexWrap="wrap"
        alignItems="center"
        gridGap={theme.spacing(5)}
        marginTop={4}
      >
        <div className={classes.drParticipantContainer}>
          <Typography variant="subtitle1">{t('participant.drPrimary')}</Typography>
          <DrParticipantCard
            xClusterConfig={xClusterConfig}
            universeXClusterRole={UniverseXClusterRole.SOURCE}
          />
        </div>
        <ReplicationIcon drConfig={drConfig} />
        <div className={classes.drParticipantContainer}>
          <Typography variant="subtitle1">{t('participant.drReplica')}</Typography>
          <DrParticipantCard
            xClusterConfig={xClusterConfig}
            universeXClusterRole={UniverseXClusterRole.TARGET}
          />
        </div>
      </Box>
    </div>
  );
};
