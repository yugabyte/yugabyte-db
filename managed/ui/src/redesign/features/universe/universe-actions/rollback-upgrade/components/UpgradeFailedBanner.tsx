import { FC, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';
import { Box, Typography } from '@material-ui/core';
import { YBButton } from '../../../../../components';
import { DBRollbackModal } from '../DBRollbackModal';
import { api } from '../../../../../utils/api';
import { SoftwareUpgradeState } from '../../../../../../components/universes/helpers/universeHelpers';
import { Universe } from '../../../universe-form/utils/dto';
import { TaskObject } from '../utils/types';
import { rollBackStyles } from '../utils/RollbackUpgradeStyles';
//icons
import ErrorIcon from '../../../../../assets/error-circle.svg';

interface RollbackBannerProps {
  taskDetail: TaskObject;
  universeData: Universe;
}

export const UpgradeFailedBanner: FC<RollbackBannerProps> = ({ universeData, taskDetail }) => {
  const { t } = useTranslation();
  const classes = rollBackStyles();
  const [openRollbackModal, setRollBackModal] = useState(false);
  const ybSoftwareUpgradeState = universeData?.universeDetails?.softwareUpgradeState;

  const redirectToTaskLogs = (taskUUID: string, universeUUID: string) => {
    taskUUID
      ? browserHistory.push(`/tasks/${taskUUID}`)
      : browserHistory.push(`/universes/${universeUUID}/tasks`);
  };

  const retryCurrentTask = async (taskUUID: string, universeUUID: string) => {
    try {
      const response = await api.retryCurrentTask(taskUUID);
      if ([200, 201].includes(response?.status)) {
        browserHistory.push(`/universes/${universeUUID}/tasks`);
      }
    } catch (e) {
      console.log(e);
    }
  };

  return (
    <Box className={classes.bannerContainer}>
      <Box display={'flex'} flexDirection={'row'} width="100%" alignItems={'center'}>
        <img src={ErrorIcon} alt="--" width="22px" height="22px" />
        &nbsp;&nbsp;
        <Typography variant="body2">
          {t('universeActions.dbRollbackUpgrade.rollback.bannerMsg')}
        </Typography>
      </Box>
      <Box display={'flex'} mt={2} flexDirection={'row'} width="100%" justifyContent={'flex-end'}>
        <YBButton
          variant="secondary"
          size="large"
          onClick={() => redirectToTaskLogs(taskDetail?.id, universeData?.universeUUID)}
          data-testid="UpgradeFailedBanner-TaskDteails"
        >
          {t('universeActions.dbRollbackUpgrade.rollback.viewTask')}
        </YBButton>
        &nbsp;&nbsp;
        {ybSoftwareUpgradeState === SoftwareUpgradeState.UPGRADE_FAILED && (
          <>
            <YBButton
              variant="secondary"
              size="large"
              onClick={() => setRollBackModal(true)}
              data-testid="UpgradeFailedBanner-RollbackButton"
            >
              {t('universeActions.dbRollbackUpgrade.rollbackUpgradeTite')}
            </YBButton>
            &nbsp;&nbsp;
          </>
        )}
        <YBButton
          variant="primary"
          size="large"
          data-testid="UpgradeFailedBanner-RetryTask"
          onClick={() => retryCurrentTask(taskDetail?.id, universeData?.universeUUID)}
        >
          {t('universeActions.dbRollbackUpgrade.rollback.retryTask')}
        </YBButton>
      </Box>
      <DBRollbackModal
        open={openRollbackModal}
        onClose={() => setRollBackModal(false)}
        universeData={universeData}
      />
    </Box>
  );
};
