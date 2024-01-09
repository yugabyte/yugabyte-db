import { FC } from 'react';
import { toast } from 'react-toastify';
import { useDispatch } from 'react-redux';
import { browserHistory } from 'react-router';
import { useMutation, useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Box, Typography, Link } from '@material-ui/core';
import { YBModal } from '../../../../../components';
import { YBLoading } from '../../../../../../components/common/indicators';
import { api } from '../../../../../utils/api';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../../../../actions/universe';
import {
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure
} from '../../../../../../actions/tasks';
import { createErrorMessage, transitToUniverse } from '../../../universe-form/utils/helpers';
import { TOAST_AUTO_DISMISS_INTERVAL } from '../../../universe-form/utils/constants';
//Rbac
import { RBAC_ERR_MSG_NO_PERM } from '../../../../rbac/common/validator/ValidatorUtils';
import { hasNecessaryPerm } from '../../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../rbac/ApiAndUserPermMapping';
//imported styles
import { preFinalizeStateStyles } from '../utils/RollbackUpgradeStyles';
//icons
import LinkIcon from '../../../../../assets/link.svg';
import { ReactComponent as FlagIcon } from '../../../../../assets/flag.svg';
import { ReactComponent as WarningBell } from '../../../../../assets/warning-bell.svg';

interface PreFinalizeModalProps {
  open: boolean;
  onClose: () => void;
  universeUUID: string;
  currentDBVersion: string;
}
const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };

export const PreFinalizeModal: FC<PreFinalizeModalProps> = ({
  open,
  onClose,
  currentDBVersion,
  universeUUID
}) => {
  const { t } = useTranslation();
  const classes = preFinalizeStateStyles();
  const dispatch = useDispatch();
  const { data: universeList, isLoading } = useQuery([], () => api.getFinalizeInfo(universeUUID));
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

  const hasImpactedXclusters = universeList?.impactedXClusterConnectedUniverse?.length > 0;
  const handleClose = async () => {
    if (hasImpactedXclusters) {
      try {
        await finalizeUpgrade.mutateAsync();
      } catch (e) {
        console.log(e);
      }
    } else onClose();
  };

  const handleSubmit = async () => {
    if (hasImpactedXclusters) {
      const path = browserHistory.getCurrentLocation().pathname;
      const pathArray = path.split('/');
      const universeUrl = `${pathArray[1]}/${pathArray[2]}`;
      browserHistory.push(`/${universeUrl}/replication`);
      onClose();
    } else {
      try {
        await finalizeUpgrade.mutateAsync();
      } catch (e) {
        console.log(e);
      }
    }
  };

  const preFinalizeBody = () => {
    return (
      <>
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
      </>
    );
  };

  const xClusterPreFinalizeBody = () => {
    return (
      <>
        {' '}
        <Typography variant="body2">
          {
            t('universeActions.dbRollbackUpgrade.preFinalizeXcluster.modalBody', {
              version: currentDBVersion
            }) as string
          }
        </Typography>
        <br />
        {universeList?.impactedXClusterConnectedUniverse?.length > 0 && (
          <Box
            display="flex"
            flexDirection={'column'}
            mt={1}
            p={2}
            style={{ backgroundColor: '#F7F7F7', border: '1px solid #E5E5E9', borderRadius: '8px' }}
          >
            <ul style={{ listStyleType: 'disc' }}>
              {universeList?.impactedXClusterConnectedUniverse?.map((e: any, i: number) => (
                <li key={i}>
                  <Box display={'flex'} flexDirection={'row'} width="100%" alignItems={'center'}>
                    <Typography variant="body1">{e?.universeName}</Typography>
                    <Box ml={2}>{e?.ybSoftwareVersion}</Box>
                  </Box>
                </li>
              ))}
            </ul>
          </Box>
        )}
      </>
    );
  };

  const canFinalizeUpgrade = hasNecessaryPerm({
    onResource: universeUUID,
    ...ApiPermissionMap.UPGRADE_UNIVERSE_FINALIZE
  });

  const modalTitle = isLoading
    ? 'Loading ...'
    : hasImpactedXclusters
    ? t('universeActions.dbRollbackUpgrade.preFinalizeXcluster.modalTitle')
    : t('universeActions.dbRollbackUpgrade.preFinalize.modalTitle');
  const titleIcon = isLoading ? null : hasImpactedXclusters ? <WarningBell /> : <FlagIcon />;
  const submitLabel = isLoading
    ? 'Loading'
    : hasImpactedXclusters
    ? t('universeActions.dbRollbackUpgrade.preFinalizeXcluster.modalSubmit')
    : t('universeActions.dbRollbackUpgrade.preFinalize.modalSubmitLabel');
  const cancelLabel = isLoading
    ? 'Loading'
    : hasImpactedXclusters
    ? t('universeActions.dbRollbackUpgrade.preFinalizeXcluster.modalProceed')
    : t('common.back');

  return (
    <YBModal
      open={open}
      overrideHeight={'340px'}
      overrideWidth={'700px'}
      cancelLabel={cancelLabel}
      submitLabel={submitLabel}
      title={modalTitle}
      size="sm"
      onClose={handleClose}
      titleSeparator
      onSubmit={handleSubmit}
      submitTestId="PreFinalizeModal-Submit"
      cancelTestId="PreFinalizeModal-Back"
      titleIcon={titleIcon}
      hideCloseBtn={hasImpactedXclusters ? true : false}
      buttonProps={{
        primary: {
          disabled: isLoading || (!hasImpactedXclusters && !canFinalizeUpgrade)
        },
        secondary: {
          disabled: isLoading || (hasImpactedXclusters && !canFinalizeUpgrade)
        }
      }}
      submitButtonTooltip={!hasImpactedXclusters && !canFinalizeUpgrade ? RBAC_ERR_MSG_NO_PERM : ''}
      cancelButtonTooltip={hasImpactedXclusters && !canFinalizeUpgrade ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      <Box className={classes.modalContainer}>
        {isLoading ? (
          <YBLoading />
        ) : hasImpactedXclusters ? (
          xClusterPreFinalizeBody()
        ) : (
          preFinalizeBody()
        )}
      </Box>
    </YBModal>
  );
};
