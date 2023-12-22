import { FC } from 'react';
import { toast } from 'react-toastify';
import { useDispatch } from 'react-redux';
import { useMutation } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Box, Typography, Link } from '@material-ui/core';
import { YBModal } from '../../../../../components';
import { api } from '../../../../../utils/api';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../../../../actions/universe';
import {
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure
} from '../../../../../../actions/tasks';
import { createErrorMessage, transitToUniverse } from '../../../universe-form/utils/helpers';
import { TOAST_AUTO_DISMISS_INTERVAL } from '../../../universe-form/utils/constants';
import { preFinalizeStateStyles } from '../utils/RollbackUpgradeStyles';
//icons
import LinkIcon from '../../../../../assets/link.svg';
import { ReactComponent as FlagIcon } from '../../../../../assets/flag.svg';

interface PreFinalizeModalProps {
  open: boolean;
  onClose: () => void;
  universeUUID: string;
}
const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };

export const PreFinalizeModal: FC<PreFinalizeModalProps> = ({ open, onClose, universeUUID }) => {
  const { t } = useTranslation();
  const classes = preFinalizeStateStyles();
  const dispatch = useDispatch();
  //Upgrade Software
  const finalizeUpgrade = useMutation(
    () => {
      return api.finalizeUpgrade(universeUUID);
    },
    {
      onSuccess: () => {
        toast.success('Finalize upgrade initiated', TOAST_OPTIONS);
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
        transitToUniverse(universeUUID);
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error), TOAST_OPTIONS);
      }
    }
  );

  const handleSubmit = async () => {
    try {
      await finalizeUpgrade.mutateAsync();
    } catch (e) {
      console.log(e);
    }
  };

  return (
    <YBModal
      open={open}
      overrideHeight={'320px'}
      overrideWidth={'610px'}
      cancelLabel={t('common.back')}
      submitLabel={t('universeActions.dbRollbackUpgrade.preFinalize.modalSubmitLabel')}
      title={t('universeActions.dbRollbackUpgrade.preFinalize.modalTitle')}
      size="sm"
      onClose={onClose}
      titleSeparator
      onSubmit={handleSubmit}
      submitTestId="PreFinalizeModal-Submit"
      cancelTestId="PreFinalizeModal-Back"
      titleIcon={<FlagIcon />}
    >
      <Box className={classes.modalContainer}>
        <Typography variant="body2">
          {t('universeActions.dbRollbackUpgrade.preFinalize.modalMsg1')}
        </Typography>
        <br />
        <Typography variant="body2">
          {t('universeActions.dbRollbackUpgrade.preFinalize.modalMsg2')}
        </Typography>
        <Box display="flex" flexDirection={'row'} alignItems={'center'} mt={3}>
          <Typography variant="body2">
            <Link underline="always" color="inherit">
              {t('universeActions.dbRollbackUpgrade.preFinalize.evaluationLink')}
            </Link>
          </Typography>
          &nbsp;
          <img src={LinkIcon} alt="---" height={'16px'} width="16px" />
        </Box>
      </Box>
    </YBModal>
  );
};
