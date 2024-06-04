import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { useMutation } from 'react-query';
import { Box, Typography, makeStyles } from '@material-ui/core';
import { YBModal } from '../../../../components';
import { ReleasesAPI } from '../../api';
import { Releases } from '../dtos';

interface DeleteReleaseModalProps {
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

export const DeleteReleaseModal = ({
  data,
  open,
  onClose,
  onActionPerformed
}: DeleteReleaseModalProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();
  const releaseUuid = data.release_uuid;

  // DELETE API call to delete the release
  const deleteRelease = useMutation(() => ReleasesAPI.deleteRelease(releaseUuid!), {
    onSuccess: (response: any) => {
      toast.success(t('releases.deleteReleaseModal.deleteReleaseSuccess'));
      onActionPerformed();
      onClose();
    },
    onError: () => {
      toast.error(t('releases.deleteReleaseModal.deleteReleaseFailure'));
    }
  });

  const handleSubmit = async () => {
    deleteRelease.mutateAsync();
  };

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t('releases.deleteReleaseModal.modalTitle')}
      onSubmit={handleSubmit}
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.delete')}
      overrideHeight="250px"
      size="sm"
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: helperClasses.root,
        dividers: true
      }}
      titleContentProps={helperClasses.modalTitle}
    >
      <Box mt={2}>
        <Typography variant="body2">
          {t('releases.deleteReleaseModal.deleteMessage', { release_version: data?.version })}
        </Typography>
      </Box>
    </YBModal>
  );
};
