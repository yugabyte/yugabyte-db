import { makeStyles, Typography } from '@material-ui/core';
import { YBButton } from '@yugabyte-ui-library/core';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';

import GrayDotIcon from '@app/redesign/assets/gray-dot.svg';
import AddIcon from '@app/redesign/assets/add.svg';
import PauseSquareIcon from '@app/redesign/assets/pause-square.svg';
import DeleteIcon from '@app/redesign/assets/delete2.svg';
import CheckIcon from '@app/redesign/assets/check.svg';

const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal.upgradePlanStep';
const TEST_ID_PREFIX = 'UpgradePlanStep';

const useStyles = makeStyles((theme) => ({
  pauseSlot: {
    display: 'flex',
    alignItems: 'center',

    padding: theme.spacing(0, 0, 0, 2),

    '&$betweenUpgradeStages': {
      padding: theme.spacing(0, 0, 0, 3.5)
    }
  },
  betweenUpgradeStages: {},
  verticalConnector: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',

    '&::before, &::after': {
      content: '""',
      width: 1,
      height: theme.spacing(4),

      backgroundColor: theme.palette.grey[300]
    }
  },
  horizontalConnector: {
    width: theme.spacing(2),
    height: 1,

    backgroundColor: theme.palette.grey[300]
  },
  pauseCard: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    padding: theme.spacing(0.75, 1.5),

    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.grey[300]}`,
    backgroundColor: theme.palette.common.white,
    color: theme.palette.grey[900],
    fontSize: '13px',
    lineHeight: '16px',
    fontWeight: 500,

    '&:hover $deleteIcon': {
      visibility: 'visible'
    }
  },
  deleteIcon: {
    visibility: 'hidden',
    cursor: 'pointer'
  },
  bodyText: {
    fontSize: '13px',
    lineHeight: '16px',
    fontWeight: 500,
    color: theme.palette.grey[900]
  },
  recommendedChip: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),

    padding: theme.spacing(0.25, 1),
    marginLeft: theme.spacing(1),

    borderRadius: '4px',
    backgroundColor: theme.palette.grey[200],
    color: theme.palette.grey[700],
    fontSize: '11.5px',
    fontWeight: 400
  }
}));

interface PauseSlotProps {
  isPaused: boolean;
  onToggle: () => void;
  isRecommended?: boolean;
  isBetweenStages?: boolean;
  testIdSuffix: string;
}

export const PauseSlot = ({
  isPaused,
  onToggle,
  isRecommended = false,
  isBetweenStages = false,
  testIdSuffix
}: PauseSlotProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

  return (
    <div className={clsx(classes.pauseSlot, isBetweenStages && classes.betweenUpgradeStages)}>
      <div className={classes.verticalConnector}>
        <GrayDotIcon />
      </div>
      <div className={classes.horizontalConnector} />
      {isPaused ? (
        <div className={classes.pauseCard}>
          <PauseSquareIcon width={13} height={13} />
          <Typography className={classes.bodyText}>{t('pauseToMonitorUpgrade')}</Typography>
          <DeleteIcon className={classes.deleteIcon} onClick={onToggle} />
        </div>
      ) : (
        <YBButton
          variant="ghost"
          onClick={onToggle}
          startIcon={<AddIcon />}
          dataTestId={`${TEST_ID_PREFIX}-AddPauseButton-${testIdSuffix}`}
        >
          {t('addMonitoringPause')}
        </YBButton>
      )}
      {isRecommended && (
        <div className={classes.recommendedChip}>
          {t('recommended', { keyPrefix: 'common' })}
          <CheckIcon />
        </div>
      )}
    </div>
  );
};
