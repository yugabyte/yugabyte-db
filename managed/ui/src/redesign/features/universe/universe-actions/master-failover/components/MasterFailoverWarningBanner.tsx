import { useTranslation } from 'react-i18next';
import { Box, Typography } from '@material-ui/core';
import { YBButton } from '../../../../../components';
import { preFinalizeStateStyles as masterFailoverStyles } from '../../rollback-upgrade/utils/RollbackUpgradeStyles';
import WarningIcon from '../../../../../assets/warning-triangle.svg?img';

interface MasterFailoverWarningBannerProps {
  duration: string;
  onSnoozeClick: () => void;
}

export const MasterFailoverWarningBanner = ({
  duration,
  onSnoozeClick
}: MasterFailoverWarningBannerProps) => {
  const { t } = useTranslation();
  const classes = masterFailoverStyles();

  return (
    <Box className={classes.bannerContainer}>
      <Box display="flex" mr={1}>
        <img src={WarningIcon} alt="---" height={'22px'} width="22px" />
      </Box>
      <Box display="flex" flexDirection={'column'} mt={0.5} width="100%">
        <Typography variant="body1">{t('universeActions.masterFailover.warning.title')}</Typography>
        <Box
          display="flex"
          flexDirection={'row'}
          width="100%"
          alignItems={'baseline'}
          justifyContent={'space-between'}
        >
          <Box mt={1}>
            <Typography variant="body2">
              {t('universeActions.masterFailover.warning.threshold', { duration })}
              {t('universeActions.masterFailover.warning.runtimeConfigMsg')}
            </Typography>
          </Box>
          <Box>
            <YBButton
              variant="secondary"
              size="large"
              onClick={() => onSnoozeClick()}
              data-testid="MasterFailoverWarningBanner-Snooze"
            >
              {t('universeActions.masterFailover.warning.snoozeBtnText')}
            </YBButton>
            &nbsp;
          </Box>
        </Box>
      </Box>
    </Box>
  );
};
