import { useTranslation } from 'react-i18next';
import { YBModal, YBModalProps } from '../../../../redesign/components';

import { DrConfig } from '../types';

interface AbortFailoverModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;
}

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.failover.abortModal';
export const AbortFailoverModal = ({ drConfig, modalProps }: AbortFailoverModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  return (
    <YBModal
      title={t('title')}
      submitLabel={t('submitButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      {...modalProps}
    >
      {t('instruction')}
      {/* TODO: Acknowledge checkbox here -- */}
    </YBModal>
  );
};
