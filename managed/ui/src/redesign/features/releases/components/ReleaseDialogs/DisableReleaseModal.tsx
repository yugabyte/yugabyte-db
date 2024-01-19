import { useTranslation } from 'react-i18next';
import { Box, Typography, makeStyles } from '@material-ui/core';
import { YBModal } from '../../../../components';
import { ReleaseState, Releases } from '../dtos';

interface DisableReleaseModalProps {
  data: Releases | null;
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

  const handleSubmit = async () => {
    // TODO: Write an useMutation call to make an API call to ensure it updates release metadata - updateReleaseMetadata (PUT) from api.ts
    // TODO: onSuccess on above mutation call, ensure to call onActionPerformed() which will get fresh set of releasaes
    // to be displayed in ReleaseList page
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
