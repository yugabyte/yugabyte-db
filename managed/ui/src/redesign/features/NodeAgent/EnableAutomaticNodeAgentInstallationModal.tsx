import { Box, Typography, useTheme } from '@material-ui/core';
import { AxiosError } from 'axios';
import { useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useMutation } from 'react-query';

import { YBInput, YBModal, YBModalProps } from '../../components';
import { RuntimeConfigKey } from '../../helpers/constants';
import { api } from '../universe/universe-form/utils/api';
import { handleServerError } from '../../../utils/errorHandlingUtils';

interface EnableAutomaticNodeAgentInstallationModalProps {
  modalProps: YBModalProps;
}

const TRANSLATION_KEY_PREFIX = 'nodeAgent.enableAutomaticNodeAgentInstallationModal';
const COMPONENT_NAME = 'EnableAutomaticNodeAgentInstallationModal';
const ACCEPTED_CONFIRMATION_TEXT = 'YES';
export const EnableAutomaticNodeAgentInstallationModal = ({
  modalProps
}: EnableAutomaticNodeAgentInstallationModalProps) => {
  const [confirmationText, setConfirmationText] = useState<string>('');
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const enableAutomaticNodeAgentInstallation = useMutation(
    () =>
      api.setRunTimeConfig({
        key: RuntimeConfigKey.ENABLE_AUTO_NODE_AGENT_INSTALLATION,
        value: true
      }),
    {
      onSuccess: () => {
        modalProps.onClose();
      },
      onError: (error: AxiosError | Error) => {
        handleServerError(error, {
          customErrorLabel: 'Failed to enable automatic node agent installation.'
        });
      }
    }
  );

  const resetModal = () => {
    setIsSubmitting(false);
    setConfirmationText('');
  };
  const onSubmit = () => {
    setIsSubmitting(true);
    enableAutomaticNodeAgentInstallation.mutate(undefined /* variables */, {
      onSettled: () => resetModal()
    });
  };

  const isFormDisabled = isSubmitting || confirmationText !== ACCEPTED_CONFIRMATION_TEXT;

  return (
    <YBModal
      title={t('title')}
      submitLabel={t('submitLabel')}
      onSubmit={onSubmit}
      showSubmitSpinner={isSubmitting}
      isSubmitting={isSubmitting}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      overrideHeight="fit-content"
      size="sm"
      submitTestId={`${COMPONENT_NAME}-SubmitButton`}
      cancelTestId={`${COMPONENT_NAME}-CancelButton`}
      {...modalProps}
    >
      <Typography variant="body2">
        <Trans i18nKey={`${TRANSLATION_KEY_PREFIX}.infoText`} components={{ paragraph: <p /> }} />
      </Typography>
      <Box marginTop={3} display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
        <Typography variant="body2">{t('confirmationText')}</Typography>
        <YBInput
          fullWidth
          placeholder={ACCEPTED_CONFIRMATION_TEXT}
          value={confirmationText}
          onChange={(event) => setConfirmationText(event.target.value)}
        />
      </Box>
    </YBModal>
  );
};
