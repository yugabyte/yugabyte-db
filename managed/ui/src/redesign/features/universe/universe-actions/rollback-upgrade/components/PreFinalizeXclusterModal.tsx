import { FC } from 'react';
import { toast } from 'react-toastify';
import { browserHistory } from 'react-router';
import { useMutation, useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Box, Typography } from '@material-ui/core';
import { YBModal } from '../../../../../components';
import { api, QUERY_KEY } from '../../../../../utils/api';
import { createErrorMessage, transitToUniverse } from '../../../universe-form/utils/helpers';
import { TOAST_AUTO_DISMISS_INTERVAL } from '../../../universe-form/utils/constants';
import { preFinalizeStateStyles } from '../utils/RollbackUpgradeStyles';
//icons
import { ReactComponent as WarningBell } from '../../../../../assets/warning-bell.svg';

interface PreFinalizeModalProps {
  open: boolean;
  onClose: () => void;
  currentDBVersion: string;
  universeUUID: string;
}
const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };

export const PreFinalizeXClusterModal: FC<PreFinalizeModalProps> = ({
  open,
  onClose,
  currentDBVersion,
  universeUUID
}) => {
  const { t } = useTranslation();
  const classes = preFinalizeStateStyles();

  const { data: universeList } = useQuery([QUERY_KEY.fetchUniverse, universeUUID], () =>
    api.getFinalizeInfo(universeUUID)
  );

  //Upgrade Software
  const finalizeUpgrade = useMutation(
    () => {
      return api.finalizeUpgrade(universeUUID);
    },
    {
      onSuccess: () => {
        toast.success('Finalize upgrade initiated', TOAST_OPTIONS);
        transitToUniverse(universeUUID);
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error), TOAST_OPTIONS);
      }
    }
  );

  const handleClose = async () => {
    try {
      await finalizeUpgrade.mutateAsync();
    } catch (e) {
      console.log(e);
    }
  };

  const handleSubmit = () => {
    const path = browserHistory.getCurrentLocation().pathname;
    let universeUrl = '';
    if (path[path.length - 1] === '/') {
      universeUrl = path.substring(0, path.lastIndexOf('/', path.length - 2));
    } else {
      universeUrl = path.substring(0, path.lastIndexOf('/'));
    }
    browserHistory.push(`${universeUrl}/replication`);
    onClose();
  };

  return (
    <YBModal
      open={open}
      overrideHeight={'340px'}
      overrideWidth={'700px'}
      cancelLabel={t('universeActions.dbRollbackUpgrade.preFinalizeXcluster.modalProceed')}
      submitLabel={t('universeActions.dbRollbackUpgrade.preFinalizeXcluster.modalSubmit')}
      title={t('universeActions.dbRollbackUpgrade.preFinalizeXcluster.modalTitle')}
      size="sm"
      onClose={handleClose}
      titleSeparator
      onSubmit={handleSubmit}
      hideCloseBtn
      submitTestId="PreFinalizXclustereModal-Submit"
      cancelTestId="PreFinalizeXclusterModal-Back"
      titleIcon={<WarningBell />}
    >
      <Box className={classes.modalContainer}>
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
      </Box>
    </YBModal>
  );
};
