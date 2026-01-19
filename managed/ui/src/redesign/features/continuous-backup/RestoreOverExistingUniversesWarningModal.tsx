import { useState } from 'react';
import { Box, Typography, useTheme } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';

import { YBInput, YBModal, YBModalProps } from '@app/redesign/components';

interface RestoreOverExistingUniversesWarningModalPropsProps {
  onSubmit: () => void;
  modalProps: YBModalProps;
}

const MODAL_NAME = 'RestoreOverExistingUniversesWarningModalProps';
const TRANSLATION_KEY_PREFIX = 'continuousBackup.restoreOverExistingUniversesWarningModal';
const CONFIRMATION_TEXT = 'Accept Risk';
export const RestoreOverExistingUniversesWarningModal = ({
  onSubmit,
  modalProps
}: RestoreOverExistingUniversesWarningModalPropsProps) => {
  const [confirmationTextInput, setConfirmationTextInput] = useState<string>('');
  const theme = useTheme();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const resetModal = () => {
    setConfirmationTextInput('');
  };
  const handleSubmit = () => {
    resetModal();
    onSubmit();
  };

  const isFormDisabled = confirmationTextInput !== CONFIRMATION_TEXT;
  return (
    <YBModal
      title={t('title')}
      submitLabel={t('submitButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={handleSubmit}
      overrideHeight="fit-content"
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      size="sm"
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      {...modalProps}
    >
      <Typography variant="body2">
        <Trans
          i18nKey={`${TRANSLATION_KEY_PREFIX}.restoreOverExistingUniversesWarning`}
          components={{ bold: <b /> }}
        />
      </Typography>
      <Box marginTop={3} display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
        <Typography variant="body2">{t('confirmationInstructions')}</Typography>
        <YBInput
          fullWidth
          placeholder={CONFIRMATION_TEXT}
          value={confirmationTextInput}
          onChange={(event) => setConfirmationTextInput(event.target.value)}
        />
      </Box>
    </YBModal>
  );
};
