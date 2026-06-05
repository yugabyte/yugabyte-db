import { makeStyles, Typography, useTheme } from '@material-ui/core';
import { YBButton, YBTag } from '@yugabyte-ui-library/core';
import clsx from 'clsx';
import { Trans, useTranslation } from 'react-i18next';

import { YBTooltip } from '@app/redesign/components';
import AddIcon from '@app/redesign/assets/add.svg';
import CheckIcon from '@app/redesign/assets/check.svg';
import DeleteIcon from '@app/redesign/assets/delete2.svg';
import PauseSquareIcon from '@app/redesign/assets/pause-square.svg';

const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal.upgradePlanStep';
const TEST_ID_PREFIX = 'UpgradePlanStep';

const PAUSE_SLOT_SPINE_WIDTH_PX = 8;

const useStyles = makeStyles((theme) => ({
  pauseSlot: {
    display: 'flex',
    alignItems: 'center',

    paddingLeft: theme.spacing(2.25),

    '&$betweenUpgradeStages': {
      paddingLeft: theme.spacing(3.5)
    }
  },
  betweenUpgradeStages: {},
  verticalConnector: {
    display: 'flex',
    flexShrink: 0,
    alignItems: 'center',

    width: theme.spacing(1)
  },
  verticalSpineSvg: {
    display: 'block',
    flexShrink: 0
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
  recommendedTag: {
    marginLeft: theme.spacing(1)
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
  const theme = useTheme();
  const classes = useStyles();

  const segmentHeightPx = parseFloat(String(theme.spacing(4)));
  const spineHeightPx = segmentHeightPx * 2 + PAUSE_SLOT_SPINE_WIDTH_PX;
  const spineCenterX = PAUSE_SLOT_SPINE_WIDTH_PX / 2;
  const dotCenterY = segmentHeightPx + PAUSE_SLOT_SPINE_WIDTH_PX / 2;
  const lineStroke = theme.palette.grey[300];

  return (
    <div className={clsx(classes.pauseSlot, isBetweenStages && classes.betweenUpgradeStages)}>
      <div className={classes.verticalConnector}>
        <svg
          className={classes.verticalSpineSvg}
          width={PAUSE_SLOT_SPINE_WIDTH_PX}
          height={spineHeightPx}
          viewBox={`0 0 ${PAUSE_SLOT_SPINE_WIDTH_PX} ${spineHeightPx}`}
          fill="none"
          xmlns="http://www.w3.org/2000/svg"
          aria-hidden
        >
          <line
            x1={spineCenterX}
            y1={0}
            x2={spineCenterX}
            y2={segmentHeightPx}
            stroke={lineStroke}
            strokeWidth={1}
          />
          <circle
            cx={spineCenterX}
            cy={dotCenterY}
            r={3.5}
            fill={theme.palette.grey[200]}
            stroke={theme.palette.grey[300]}
          />
          <line
            x1={spineCenterX}
            y1={segmentHeightPx + PAUSE_SLOT_SPINE_WIDTH_PX}
            x2={spineCenterX}
            y2={spineHeightPx}
            stroke={lineStroke}
            strokeWidth={1}
          />
        </svg>
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
        <YBTooltip
          title={
            <Typography variant="body2" component="span">
              <Trans t={t} i18nKey="pauseAfterFirstAzTooltip" components={{ bold: <b /> }} />
            </Typography>
          }
        >
          <div className={classes.recommendedTag}>
            <YBTag variant="dark" size="small" endIcon={<CheckIcon />}>
              {t('recommended', { keyPrefix: 'common' })}
            </YBTag>
          </div>
        </YBTooltip>
      )}
    </div>
  );
};
