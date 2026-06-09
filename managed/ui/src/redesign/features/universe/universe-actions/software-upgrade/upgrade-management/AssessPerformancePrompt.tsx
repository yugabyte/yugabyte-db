import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import TrendSparklineIcon from '@app/redesign/assets/approved/trend-sparkline.svg';

import type { UpgradeStageCategory } from './constants';

const useStyles = makeStyles((theme) => ({
  elevatedInfoCard: {
    display: 'flex',
    gap: theme.spacing(2),

    padding: theme.spacing(2),

    color: theme.palette.grey[900],
    backgroundColor: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius,
    boxShadow: '0px 4px 12px 0px rgba(14, 28, 41, 0.07)'
  },
  trendLineIconContainer: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',

    minWidth: 44,
    minHeight: 44,
    maxWidth: 44,
    maxHeight: 44,

    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: '50%'
  },
  content: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  }
}));

interface AssessPerformancePromptProps {
  upgradeStageCategory: UpgradeStageCategory;
}

const TRANSLATION_KEY_PREFIX =
  'universeActions.dbUpgrade.dbUpgradeManagementSidePanel.progressPanel';

export const AssessPerformancePrompt = ({ upgradeStageCategory }: AssessPerformancePromptProps) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });

  return (
    <div className={classes.elevatedInfoCard}>
      <div className={classes.trendLineIconContainer}>
        <TrendSparklineIcon width={24} height={24} />
      </div>
      <div className={classes.content}>
        <Typography variant="body1">
          {t(`assessPerformance.${upgradeStageCategory}.title`)}
        </Typography>
        <Typography variant="body2">
          {t(`assessPerformance.${upgradeStageCategory}.description`)}
        </Typography>
      </div>
    </div>
  );
};
