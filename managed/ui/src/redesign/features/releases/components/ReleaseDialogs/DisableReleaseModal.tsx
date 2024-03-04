import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useMutation } from 'react-query';
import { toast } from 'react-toastify';
import { Box, Typography, makeStyles } from '@material-ui/core';
import { YBModal } from '../../../../components';
import { ReleaseState, Releases } from '../dtos';
import { ReleasesAPI } from '../../api';

interface DisableReleaseModalProps {
  data: Releases;
  open: boolean;
  onClose: () => void;
  onActionPerformed: () => void;
}

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `${theme.spacing(3)}px ${theme.spacing(4.5)}px`
  },
  modalTitle: {
    marginLeft: theme.spacing(2.25)
  }
}));

export const DisableReleaseModal = ({
  data,
  open,
  onClose,
  onActionPerformed
}: DisableReleaseModalProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();
  const releaseUuid = data.release_uuid;

  // State variable
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);

  // PUT API call to disable the release
  const disableRelease = useMutation(
    (payload: any) => ReleasesAPI.updateReleaseMetadata(payload, releaseUuid!),
    {
      onSuccess: (response: any) => {
        toast.success('Disabled release successfully');
        onActionPerformed();
        onClose();
      },
      onError: () => {
        toast.error('Failed to disable release');
      }
    }
  );

  const handleSubmit = () => {
    const payload: any = {};
    Object.assign(payload, data);
    payload.state =
      data.state === ReleaseState.ACTIVE ? ReleaseState.DISABLED : ReleaseState.ACTIVE;
    setIsSubmitting(true);
    disableRelease.mutate(payload, { onSettled: () => resetModal() });
  };

  const resetModal = () => {
    setIsSubmitting(false);
  };

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={
        data?.state === ReleaseState.ACTIVE
          ? t('releases.disableReleaseModal.disableRelease')
          : t('releases.disableReleaseModal.enableRelease')
      }
      onSubmit={handleSubmit}
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.disable')}
      overrideHeight="250px"
      size="sm"
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: helperClasses.root,
        dividers: true
      }}
      isSubmitting={isSubmitting}
      titleContentProps={helperClasses.modalTitle}
    >
      <Box mt={2}>
        <Typography variant="body2">
          {t('releases.disableReleaseModal.disableMessage', { release_version: data?.version })}
        </Typography>
      </Box>
    </YBModal>
  );
};
