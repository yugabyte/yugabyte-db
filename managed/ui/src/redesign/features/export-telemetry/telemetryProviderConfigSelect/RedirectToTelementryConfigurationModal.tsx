import { Trans, useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';

import { YBModal, YBModalProps } from '@app/redesign/components';

const TRANSLATION_KEY_PREFIX = 'telemetryProviderConfigSelect.redirectModal';
const TELEMETRY_CONFIGURATION_PAGE = '/config/exportTelemetry';

export const RedirectToTelemetryConfigurationModal = ({ onClose, open }: YBModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const redirectToTelemetryConfigurationPage = () =>
    browserHistory.push(TELEMETRY_CONFIGURATION_PAGE);
  return (
    <YBModal
      title={t('title')}
      cancelLabel={t('cancelButton')}
      submitLabel={t('submitButton')}
      overrideWidth="500px"
      overrideHeight="291px"
      open={open}
      onClose={onClose}
      onSubmit={redirectToTelemetryConfigurationPage}
    >
      <Trans i18nKey={`${TRANSLATION_KEY_PREFIX}.message`} components={{ bold: <b /> }} />
    </YBModal>
  );
};
