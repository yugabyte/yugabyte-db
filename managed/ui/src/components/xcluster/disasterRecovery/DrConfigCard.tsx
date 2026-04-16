import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { Link } from 'react-router';
import clsx from 'clsx';

import {
  TRANSITORY_XCLUSTER_CONFIG_STATUSES,
  UniverseXClusterRole,
  XClusterConfigStatus
} from '../constants';
import { ReplicationIcon } from '../icons/ReplicationIcon';
import { DrParticipantCard } from './drConfig/DrParticipantCard';
import { getXClusterConfig } from './utils';
import { DrConfigStateLabel } from './DrConfigStateLabel';
import { EstimatedDataLossLabel } from './drConfig/EstimatedDataLossLabel';

import { DrConfig } from './dtos';

import styles from './DrConfigCard.module.scss';

interface DrConfigCardProps {
  drConfig: DrConfig;
  currentUniverseUuid: string;
}

const useStyles = makeStyles((theme) => ({
  drParticipantContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  }
}));

const DR_PARTICIPANT_CARD_WIDTH_PX = 350;

export const DrConfigCard = ({ drConfig, currentUniverseUuid }: DrConfigCardProps) => {
  const theme = useTheme();
  const classes = useStyles();

  const xClusterConfig = getXClusterConfig(drConfig);
  const shouldShowTaskLink =
    xClusterConfig.status === XClusterConfigStatus.FAILED ||
    TRANSITORY_XCLUSTER_CONFIG_STATUSES.includes(xClusterConfig.status);
  return (
    <div className={styles.configCard}>
      <Link to={`/universes/${currentUniverseUuid}/recovery/${drConfig.uuid}`}>
        <div className={styles.headerSection}>
          <div className={styles.configNameContainer}>
            <div className={styles.configName}>{xClusterConfig.name}</div>
          </div>
          <div className={styles.status}>
            <DrConfigStateLabel drConfig={drConfig} variant="h5" />
          </div>
        </div>
      </Link>
      <div className={styles.bodySection}>
        <Box display="flex" flexWrap="wrap" alignItems="center" gridGap={theme.spacing(5)}>
          <div className={classes.drParticipantContainer}>
            <Typography variant="subtitle1">DR Primary</Typography>
            <DrParticipantCard
              xClusterConfig={xClusterConfig}
              universeXClusterRole={UniverseXClusterRole.SOURCE}
              width={DR_PARTICIPANT_CARD_WIDTH_PX}
            />
          </div>
          <ReplicationIcon drConfig={drConfig} />
          <div className={classes.drParticipantContainer}>
            <Typography variant="subtitle1">DR Replica</Typography>
            <DrParticipantCard
              xClusterConfig={xClusterConfig}
              universeXClusterRole={UniverseXClusterRole.TARGET}
              width={DR_PARTICIPANT_CARD_WIDTH_PX}
            />
          </div>
        </Box>
        {shouldShowTaskLink ? (
          <div className={styles.viewTasksPrompt}>
            <span>View progress on </span>
            <a href={`/universes/${xClusterConfig.sourceUniverseUUID}/tasks`}>Tasks</a>.
          </div>
        ) : (
          <div className={styles.configMetricsContainer}>
            <div className={clsx(styles.configMetric, styles.currentLag)}>
              <div className={styles.label}>Estimated Data Loss</div>
              <EstimatedDataLossLabel drConfigUuid={drConfig.uuid} />
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
