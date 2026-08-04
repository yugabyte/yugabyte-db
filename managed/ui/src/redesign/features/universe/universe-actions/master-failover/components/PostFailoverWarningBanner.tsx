import { Box, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBButton } from '../../../../../components';
import { preFinalizeStateStyles as masterFailoverStyles } from '../../rollback-upgrade/utils/RollbackUpgradeStyles';
import WarningIcon from '../../../../../assets/warning-triangle.svg?img';

interface PostFailoverWarningBannerProps {
  duration: string;
}

export const PostFailoverWarningBanner = ({ duration }: PostFailoverWarningBannerProps) => {
  const { t } = useTranslation();
  const classes = masterFailoverStyles();

  return (
    <Box className={classes.bannerContainer}>
      <Box display="flex" mr={1}>
        <img src={WarningIcon} alt="---" height={'22px'} width="22px" />
      </Box>
      <Box display="flex" flexDirection={'column'} mt={0.5} width="100%">
        <Typography variant="body1">{t('universeActions.postFailover.warning.title')}</Typography>
        <Box
          display="flex"
          flexDirection={'row'}
          width="100%"
          alignItems={'baseline'}
          justifyContent={'space-between'}
        >
          <Box mt={1}>
            <Typography variant="body2">
              {t('universeActions.postFailover.warning.threshold', { duration })}
              {t('universeActions.postFailover.warning.manualCleanup')}
            </Typography>
          </Box>
        </Box>
      </Box>
    </Box>
  );
};
