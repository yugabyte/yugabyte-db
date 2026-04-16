import { FC } from 'react';
import _ from 'lodash';
import { toast } from 'react-toastify';
import { useDispatch } from 'react-redux';
import { useMutation } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { useForm, FormProvider } from 'react-hook-form';
import { Box, Typography } from '@material-ui/core';
import { YBModal, YBCheckboxField, YBInputField, YBLabel } from '../../../../components';
import { api } from '../../../../utils/api';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../../../actions/universe';
import { useIsTaskNewUIEnabled } from '../../../tasks/TaskUtils';
import {
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure,
  showTaskInDrawer
} from '../../../../../actions/tasks';
import { createErrorMessage, transitToUniverse } from '../../universe-form/utils/helpers';
import { getIsKubernetesUniverse } from '@app/utils/UniverseUtils';
import { Universe } from '../../universe-form/utils/dto';
import { TOAST_AUTO_DISMISS_INTERVAL } from '../../universe-form/utils/constants';
import { DBRollbackFormFields, UPGRADE_TYPE, DBRollbackPayload } from './utils/types';
//Rbac
import { RBAC_ERR_MSG_NO_PERM } from '../../../rbac/common/validator/ValidatorUtils';
import { hasNecessaryPerm } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
//imported styles
import { dbUpgradeFormStyles } from './utils/RollbackUpgradeStyles';
//icons
import ClockRewindIcon from '../../../../assets/clock-rewind.svg';
import WarningIcon from '../../../../assets/warning-triangle.svg?img';

interface DBRollbackModalProps {
  open: boolean;
  onClose: () => void;
  universeData: Universe;
}
const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };

export const DBRollbackModal: FC<DBRollbackModalProps> = ({ open, onClose, universeData }) => {
  const { t } = useTranslation();
  const classes = dbUpgradeFormStyles();
  const { universeDetails, universeUUID } = universeData;
  const prevVersion = _.get(universeDetails, 'prevYBSoftwareConfig.softwareVersion', '');
  const isK8sUniverse = getIsKubernetesUniverse(universeData);
  const dispatch = useDispatch();
  const isNewTaskUIEnabled = useIsTaskNewUIEnabled();
  const formMethods = useForm<DBRollbackFormFields>({
    defaultValues: {
      rollingUpgrade: true,
      timeDelay: 180
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const { control, handleSubmit, watch } = formMethods;
  const isRollingUpgrade = watch('rollingUpgrade');

  //rollback upgrade
  const rollingUpgrade = useMutation(
    (values: DBRollbackPayload) => {
      return api.rollbackUpgrade(universeUUID, values);
    },
    {
      onSuccess: (resp) => {
        toast.success('Rollback form submitted successfully', TOAST_OPTIONS);
        dispatch(fetchCustomerTasks() as any).then((response: any) => {
          if (!response.error) {
            dispatch(fetchCustomerTasksSuccess(response.payload));
          } else {
            dispatch(fetchCustomerTasksFailure(response.payload));
          }
        });
        //Universe upgrade state is not updating immediately
        setTimeout(() => {
          dispatch(fetchUniverseInfo(universeUUID) as any).then((response: any) => {
            dispatch(fetchUniverseInfoResponse(response.payload));
          });
        }, 2000);
        if (isNewTaskUIEnabled) {
          dispatch(showTaskInDrawer(resp.taskUUID));
        } else {
          transitToUniverse(universeUUID);
        }
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error), TOAST_OPTIONS);
      }
    }
  );

  const handleFormSubmit = handleSubmit(async (values) => {
    const payload: DBRollbackPayload = {
      sleepAfterMasterRestartMillis: Number(values.timeDelay) * 1000,
      sleepAfterTServerRestartMillis: Number(values.timeDelay) * 1000,
      upgradeOption: values.rollingUpgrade ? UPGRADE_TYPE.ROLLING : UPGRADE_TYPE.NON_ROLLING
    };
    try {
      await rollingUpgrade.mutateAsync(payload);
    } catch (e) {
      console.log(e);
    }
  });

  const canRollbackUpgrade = hasNecessaryPerm({
    onResource: universeUUID,
    ...ApiPermissionMap.UPGRADE_UNIVERSE_ROLLBACK
  });

  return (
    <YBModal
      open={open}
      overrideHeight={'420px'}
      overrideWidth={'610px'}
      cancelLabel={t('common.cancel')}
      submitLabel={t('universeActions.dbRollbackUpgrade.proceedRollback')}
      title={t('universeActions.dbRollbackUpgrade.rollbackUpgradeTite')}
      size="sm"
      onClose={onClose}
      titleSeparator
      onSubmit={handleFormSubmit}
      submitTestId="RollBackUpgrade-Submit"
      cancelTestId="RollBackUpgrade-Back"
      titleIcon={<ClockRewindIcon />}
      buttonProps={{
        primary: {
          disabled: !canRollbackUpgrade
        }
      }}
      submitButtonTooltip={!canRollbackUpgrade ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      <FormProvider {...formMethods}>
        <Box
          display="flex"
          width="100%"
          height="100%"
          flexDirection={'column'}
          style={{ padding: '16px 20px 16px 8px' }}
        >
          <Typography variant="body2">
            {t('universeActions.dbRollbackUpgrade.rollbackModalMsg')}
            <b>
              {t('universeActions.dbRollbackUpgrade.version')} {prevVersion}?
            </b>
          </Typography>
          <Box className={classes.upgradeDetailsContainer}>
            <YBCheckboxField
              name={'rollingUpgrade'}
              label={t('universeActions.dbRollbackUpgrade.rollbackOnenode')}
              control={control}
              style={{ width: 'fit-content' }}
              inputProps={{
                'data-testid': 'RotateUniverseKey-Checkbox'
              }}
              disabled={isK8sUniverse}
            />
            <Box
              display={'flex'}
              flexDirection={'row'}
              mt={1}
              ml={1}
              width="100%"
              alignItems={'center'}
            >
              <YBLabel width="170px">
                {t('universeActions.dbRollbackUpgrade.delayBwRollback')}
              </YBLabel>
              <Box width="160px" mr={1}>
                <YBInputField
                  control={control}
                  type="number"
                  name="timeDelay"
                  fullWidth
                  disabled={!isRollingUpgrade}
                  inputProps={{
                    autoFocus: true,
                    'data-testid': 'time-delay'
                  }}
                />
              </Box>
              <Typography variant="body2">{t('common.seconds')}</Typography>
            </Box>
          </Box>
          {!isRollingUpgrade && (
            <Box mt={1} className={classes.warningBanner}>
              <Box display="flex" mr={1}>
                <img src={WarningIcon} alt="---" height={'22px'} width="22px" />
              </Box>
              <Box display={'flex'} flexDirection={'column'}>
                <Typography variant="body1"></Typography>
                <Typography variant="body2">
                  <Trans i18nKey="universeActions.dbRollbackUpgrade.nonRollingWarning" />
                </Typography>
              </Box>
            </Box>
          )}
        </Box>
      </FormProvider>
    </YBModal>
  );
};
