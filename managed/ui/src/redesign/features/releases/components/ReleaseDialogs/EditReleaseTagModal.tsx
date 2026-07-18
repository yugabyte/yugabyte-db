import { useState } from 'react';
import { useMutation } from 'react-query';
import { useForm, FormProvider } from 'react-hook-form';
import { toast } from 'react-toastify';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles } from '@material-ui/core';
import { YBInputField, YBLabel, YBModal } from '../../../../components';
import { ReleasesAPI } from '../../api';
import { EditReleaseTagFormFields, ModalTitle, Releases } from '../dtos';

interface EditReleaseTagModalProps {
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

export const EditReleaseTagModal = ({
  data,
  open,
  onClose,
  onActionPerformed
}: EditReleaseTagModalProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();
  const releaseUuid = data.release_uuid;

  // State variable
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);

  const releaseTag = data?.release_tag;
  const formMethods = useForm<EditReleaseTagFormFields>({
    defaultValues: {
      releaseTag: releaseTag
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const {
    formState: { isDirty },
    control,
    handleSubmit
  } = formMethods;

  // PUT API call to edit/add release tags to existing release
  const updateReleaseMetadata = useMutation(
    (payload: any) => ReleasesAPI.updateReleaseMetadata(payload, releaseUuid!),
    {
      onSuccess: (response: any) => {
        toast.success(t('releases.editReleaseTagModal.updateReleaseTagSuccess'));
        onActionPerformed();
        onClose();
      },
      onError: () => {
        toast.error(t('releases.editReleaseTagModal.updateReleaseTagFailure'));
      }
    }
  );

  const handleFormSubmit = handleSubmit((formValues) => {
    const payload: any = {};
    Object.assign(payload, data);
    payload.release_tag = formValues.releaseTag;
    setIsSubmitting(true);
    // updateReleaseMetadata.mutateAsync(payload);
    updateReleaseMetadata.mutate(payload, { onSettled: () => resetModal() });
  });

  const resetModal = () => {
    setIsSubmitting(false);
  };

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={ModalTitle.EDIT_RELEASE_TAG}
      onSubmit={handleFormSubmit}
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.save')}
      overrideHeight="300px"
      size="sm"
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: helperClasses.root,
        dividers: true
      }}
      isSubmitting={isSubmitting}
      titleContentProps={helperClasses.modalTitle}
      buttonProps={{
        primary: {
          disabled: !isDirty
        }
      }}
    >
      <FormProvider {...formMethods}>
        <Box>
          <YBLabel>{t('releases.tag')}</YBLabel>
          <Box mt={1}>
            <YBInputField name={'releaseTag'} control={control} fullWidth />
          </Box>
        </Box>
      </FormProvider>
    </YBModal>
  );
};
