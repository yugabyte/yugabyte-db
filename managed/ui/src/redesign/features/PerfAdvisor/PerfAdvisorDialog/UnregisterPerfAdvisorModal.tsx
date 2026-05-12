import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { YBCheckbox, YBModal } from '../../../components';
import { PerfAdvisorAPI, QUERY_KEY } from '../api';

interface UnregisterPerfAdvisorModalProps {
  open: boolean;
  onRefetchConfig: () => void;
  onClose: () => void;
  data: any;
}

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `${theme.spacing(3)}px ${theme.spacing(4.5)}px`
  },
  modalTitle: {
    marginLeft: theme.spacing(2.25)
  }
}));

export const UnregisterPerfAdvisorModal = ({
  open,
  onRefetchConfig,
  onClose,
  data
}: UnregisterPerfAdvisorModalProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();
  const queryClient = useQueryClient();

  const [forceUnregister, setForceUnregister] = useState<boolean>(false);
  const paUuid = data.paUuid;

  // DELETE API call to unregister the Troubleshooting Platform service
  const unregisterTPService = useMutation(
    () => PerfAdvisorAPI.unRegisterPerfAdvisor(paUuid, forceUnregister),
    {
      onSuccess: (response: any) => {
        toast.success(t('clusterDetail.troubleshoot.deleteDialog.unregistrationSuccess'));
        queryClient.invalidateQueries(QUERY_KEY.fetchPerfAdvisorList);
        onRefetchConfig();
        onClose();
      },
      onError: () => {
        toast.error(t('clusterDetail.troubleshoot.deleteDialog.unregistrationFailed'));
      }
    }
  );

  const handleSubmit = async () => {
    unregisterTPService.mutateAsync();
  };

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t('clusterDetail.troubleshoot.deleteDialog.title')}
      onSubmit={handleSubmit}
      cancelLabel={t('common.cancel')}
      submitLabel={t('clusterDetail.troubleshoot.deleteDialog.unregisterButton')}
      overrideHeight="250px"
      size="sm"
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: helperClasses.root,
        dividers: true
      }}
      isSubmitting={unregisterTPService.isLoading}
      submitButtonTooltip={
        data.inUseStatus ? (
          <Typography
            variant="body2"
            style={{
              fontSize: '13px',
              fontWeight: 400,
              lineHeight: 1.25
            }}
          >
            {t('clusterDetail.troubleshoot.deleteDialog.tooltipMessage')}
          </Typography>
        ) : (
          ''
        )
      }
      footerAccessory={
        <YBCheckbox
          checked={forceUnregister}
          onChange={() => setForceUnregister(!forceUnregister)}
          label={t('clusterDetail.troubleshoot.deleteDialog.forceUnregisterCheckBoxLabel')}
          inputProps={{
            'data-testid': 'UnregisterPerfAdvisorModal-ForceUnregister'
          }}
        />
      }
      buttonProps={{
        primary: { disabled: data.inUseStatus && !forceUnregister }
      }}
      titleContentProps={helperClasses.modalTitle}
    >
      <Box mt={2}>
        <Typography variant="body2">
          {t('clusterDetail.troubleshoot.deleteDialog.unregisterMessage', {
            url: data.paUrl
          })}
        </Typography>
      </Box>
    </YBModal>
  );
};
