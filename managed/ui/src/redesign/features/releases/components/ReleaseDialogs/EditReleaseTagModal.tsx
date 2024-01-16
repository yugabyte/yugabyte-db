import { Box, makeStyles } from '@material-ui/core';
import { useForm, FormProvider } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { YBInputField, YBLabel, YBModal } from '../../../../components';
import { EditReleaseTagFormFields, ModalTitle, Releases } from '../dtos';

interface EditReleaseTagModalProps {
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

export const EditReleaseTagModal = ({
  data,
  open,
  onClose,
  onActionPerformed
}: EditReleaseTagModalProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();

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

  const handleFormSubmit = handleSubmit((formValues) => {
    // TODO: Write a usemutation call to update release metadata - updateReleaseMetadata (PUT) from api.ts
    // TODO: onSuccess on above mutation call, ensure to call onActionPerformed() which will get fresh set of releasaes
    // to be displayed in ReleaseList page
  });

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
