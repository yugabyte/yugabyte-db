import { Box, useTheme } from '@material-ui/core';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { YBButton } from '../../components';
import { CreateYbaBackupModal } from './CreateYbaBackupModal';

const TRANSLATION_KEY_PREFIX = 'continuousBackup.actionBar';

export const ContinuousBackupActionBar = () => {
  const [isCreateYbaBackupModalOpen, setIsCreateYbaBackupModalOpen] = useState(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();

  const openCreateYbaBackupModal = () => setIsCreateYbaBackupModalOpen(true);
  const closeCreateYbaBackupModal = () => setIsCreateYbaBackupModalOpen(false);

  return (
    <Box display="flex" gridGap={theme.spacing(1)}>
      <YBButton
        variant="secondary"
        onClick={openCreateYbaBackupModal}
        data-testid="ContinuousBackupActionBar-OneTimeExportButton"
      >
        {t('oneTimeBackup')}
      </YBButton>
      {isCreateYbaBackupModalOpen && (
        <CreateYbaBackupModal
          modalProps={{ open: isCreateYbaBackupModalOpen, onClose: closeCreateYbaBackupModal }}
        />
      )}
    </Box>
  );
};
