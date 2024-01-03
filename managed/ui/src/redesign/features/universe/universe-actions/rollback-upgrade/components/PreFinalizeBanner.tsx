import { useState, FC } from 'react';
import _ from 'lodash';
import { useTranslation } from 'react-i18next';
import { Box, Typography, Link } from '@material-ui/core';
import { YBButton } from '../../../../../components';
import { DBRollbackModal } from '../DBRollbackModal';
import { PreFinalizeModal } from './PreFinalizeModal';
import { PreFinalizeXClusterModal } from './PreFinalizeXclusterModal';
import { getPrimaryCluster } from '../../../universe-form/utils/helpers';
import { Universe } from '../../../universe-form/utils/dto';
import { preFinalizeStateStyles } from '../utils/RollbackUpgradeStyles';
//icons
import WarningIcon from '../../../../../assets/warning-triangle.svg';
import LinkIcon from '../../../../../assets/link.svg';

interface PreFinalizeBannerProps {
  universeData: Universe;
}

export const PreFinalizeBanner: FC<PreFinalizeBannerProps> = ({ universeData }) => {
  const { universeUUID, universeDetails } = universeData;
  const prevVersion = _.get(universeDetails, 'prevYBSoftwareConfig.softwareVersion', '');
  const { t } = useTranslation();
  const [openPreFinalModal, setPreFinalModal] = useState(false);
  const [openRollBackModal, setRollBackModal] = useState(false);
  const primaryCluster = _.cloneDeep(getPrimaryCluster(universeDetails));
  const currentRelease = primaryCluster?.userIntent.ybSoftwareVersion;
  const universeHasXcluster =
    universeDetails?.xclusterInfo?.sourceXClusterConfigs?.length > 0 ||
    universeDetails?.xclusterInfo?.targetXClusterConfigs?.length > 0;
  const classes = preFinalizeStateStyles();

  return (
    <Box className={classes.bannerContainer}>
      <Box display="flex" mr={1}>
        <img src={WarningIcon} alt="---" height={'22px'} width="22px" />
      </Box>
      <Box display="flex" flexDirection={'column'} mt={0.5} width="100%">
        <Typography variant="body1">
          {t('universeActions.dbRollbackUpgrade.preFinalize.bannerMsg1')}
        </Typography>
        <Box display="flex" mt={1.5} mb={2} flexDirection={'row'} alignItems={'center'}>
          <Typography variant="body2">
            {t('universeActions.dbRollbackUpgrade.preFinalize.bannerMsg2')}{' '}
            <Link underline="always" color="inherit">
              {t('universeActions.dbRollbackUpgrade.preFinalize.evaluationLink')}
            </Link>
          </Typography>
          &nbsp;
          <img src={LinkIcon} alt="---" height={'16px'} width="16px" />
        </Box>
        <Typography variant="body2">
          {t('universeActions.dbRollbackUpgrade.preFinalize.bannerMsg3')}
          <b>{t('universeActions.dbRollbackUpgrade.preFinalize.finalizeUpgrade')}</b>{' '}
          {t('universeActions.dbRollbackUpgrade.preFinalize.bannerMsg4')}
          <b>{t('universeActions.dbRollbackUpgrade.preFinalize.rollback')}</b>
          {t('universeActions.dbRollbackUpgrade.preFinalize.bannerMsg5')}
        </Typography>
        <Box display="flex" flexDirection={'row'} width="100%" justifyContent={'flex-end'} mt={2}>
          <YBButton
            variant="secondary"
            size="large"
            onClick={() => setRollBackModal(true)}
            data-testid="PreFinalizeBanner-Rollback"
          >
            {t('universeActions.dbRollbackUpgrade.preFinalize.rollback')} ({prevVersion})
          </YBButton>
          &nbsp;
          <YBButton
            variant="primary"
            size="large"
            data-testid="PreFinalizeButton"
            onClick={() => setPreFinalModal(true)}
          >
            {t('universeActions.dbRollbackUpgrade.preFinalize.finalizeUpgrade')}
          </YBButton>
        </Box>
      </Box>
      (
      {openPreFinalModal && (
        <PreFinalizeModal
          open={openPreFinalModal}
          universeUUID={universeUUID}
          currentDBVersion={currentRelease ?? ''}
          onClose={() => setPreFinalModal(false)}
        />
      )}
      <DBRollbackModal
        open={openRollBackModal}
        universeData={universeData}
        onClose={() => setRollBackModal(false)}
      />
    </Box>
  );
};
