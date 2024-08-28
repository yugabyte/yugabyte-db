import { Box, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBButton } from '../../../../../components';
import { preFinalizeStateStyles as masterFailoverStyles } from '../../rollback-upgrade/utils/RollbackUpgradeStyles';
import WarningIcon from '../../../../../assets/warning-triangle.svg';

interface PostFailoverWarningBannerProps {
  duration: string;
  onSnoozeClick: () => void;
}

export const PostFailoverWarningBanner = ({
  duration,
  onSnoozeClick
}: PostFailoverWarningBannerProps) => {
  const { t } = useTranslation();
  const classes = masterFailoverStyles();

  return (
    <Box className={classes.bannerContainer}>
      <Box display="flex" mr={1}>
        <img src={WarningIcon} alt="---" height={'22px'} width="22px" />
      </Box>
      <Box display="flex" flexDirection={'column'} mt={0.5} width="100%">
        <Typography variant="body1">
          {t('universeActions.postFailover.warning.title', { duration })}
        </Typography>
        <Typography variant="body2">{t('universeActions.postFailover.warning.disable')}</Typography>
        <Box display="flex" flexDirection={'row'} width="100%" justifyContent={'flex-end'} mt={2}>
          <YBButton
            variant="secondary"
            size="large"
            onClick={() => onSnoozeClick()}
            data-testid="PostFailoverWarningBanner-Snooze"
          >
            {t('universeActions.postFailover.warning.snoozeBtnText')}
          </YBButton>
          &nbsp;
        </Box>
      </Box>
    </Box>
  );
};
