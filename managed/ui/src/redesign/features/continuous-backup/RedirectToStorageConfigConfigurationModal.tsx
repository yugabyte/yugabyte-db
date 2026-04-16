import { Trans, useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';
import { YBModal, YBModalProps } from '../../components';

const TRANSLATION_KEY_PREFIX = 'component.backupStorageConfigSelect.redirectModal';
const BACKUP_STORAGE_CONFIG_PAGE = '/config/backup';

export const RedirectToStorageConfigConfigurationModal = ({ onClose, open }: YBModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const redirectToStorageConfigConfigurationPage = () =>
    browserHistory.push(BACKUP_STORAGE_CONFIG_PAGE);
  return (
    <YBModal
      title={t('title')}
      cancelLabel={t('cancelButton')}
      submitLabel={t('submitButton')}
      overrideWidth="500px"
      overrideHeight="291px"
      open={open}
      onClose={onClose}
      onSubmit={redirectToStorageConfigConfigurationPage}
    >
      <Trans i18nKey={`${TRANSLATION_KEY_PREFIX}.message`} components={{ bold: <b /> }} />
    </YBModal>
  );
};
