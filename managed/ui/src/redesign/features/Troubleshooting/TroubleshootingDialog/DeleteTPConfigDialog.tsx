import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useMutation } from 'react-query';
import { toast } from 'react-toastify';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { YBCheckbox, YBModal } from '../../../components';
import { TroubleshootingAPI } from '../api';

interface DeleteTPConfigDialogProps {
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

export const DeleteTPConfigDialog = ({
  open,
  onRefetchConfig,
  onClose,
  data
}: DeleteTPConfigDialogProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();

  const [forceUnregister, setForceUnregister] = useState<boolean>(false);
  const tpUuid = data.tpUuid;

  // DELETE API call to unregister the Troubleshooting Platform service
  const unregisterTPService = useMutation(
    () => TroubleshootingAPI.deleteTp(tpUuid, forceUnregister),
    {
      onSuccess: (response: any) => {
        toast.success(t('clusterDetail.troubleshoot.deleteDialog.unregistrationSuccess'));
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
            'data-testid': 'DeleteTPConfigDialog-ForceUnregister'
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
            url: data.tpUrl
          })}
        </Typography>
      </Box>
    </YBModal>
  );
};
