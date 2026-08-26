import { Trans, useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';

import { YBModal, YBModalProps } from '@app/redesign/components';
import { EditUniverseTabs } from '@app/redesign/features-v2/universe/edit-universe/EditUniverseContext';
import { getEditUniverseSettingsRoute } from '@app/redesign/features-v2/universe/edit-universe/editUniverseTabUtils';

const TRANSLATION_KEY_PREFIX = 'universeMetricsExport.redirectToSettingsModal';

interface RedirectToUniverseTelemetryExportModalProps extends YBModalProps {
  universeUuid: string;
}

export const RedirectToUniverseTelemetryExportModal = ({
  universeUuid,
  onClose,
  open
}: RedirectToUniverseTelemetryExportModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const redirectToUniverseTelemetryExportPage = () =>
    browserHistory.push(
      getEditUniverseSettingsRoute(universeUuid, EditUniverseTabs.TELEMETRY_EXPORT)
    );

  return (
    <YBModal
      title={t('title')}
      cancelLabel={t('cancelButton')}
      submitLabel={t('submitButton')}
      overrideWidth="400px"
      overrideHeight="274px"
      open={open}
      onClose={onClose}
      onSubmit={redirectToUniverseTelemetryExportPage}
    >
      <Trans i18nKey={`${TRANSLATION_KEY_PREFIX}.message`} components={{ bold: <b /> }} />
    </YBModal>
  );
};
