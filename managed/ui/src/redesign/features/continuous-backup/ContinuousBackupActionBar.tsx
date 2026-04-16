import { Box, useTheme } from '@material-ui/core';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';

import { YBButton } from '../../components';
import { ApiPermissionMap } from '../rbac/ApiAndUserPermMapping';
import { hasNecessaryPerm, RbacValidator } from '../rbac/common/RbacApiPermValidator';
import { CreateYbaBackupModal } from './CreateYbaBackupModal';
import { RestoreYbaBackupModal } from './RestoreYbaBackupModal';

const TRANSLATION_KEY_PREFIX = 'continuousBackup.actionBar';

export const ContinuousBackupActionBar = () => {
  const [isCreateYbaBackupModalOpen, setIsCreateYbaBackupModalOpen] = useState(false);
  const [isRestoreYbaBackupModalOpen, setIsRestoreYbaBackupModalOpen] = useState(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();

  const openCreateYbaBackupModal = () => setIsCreateYbaBackupModalOpen(true);
  const closeCreateYbaBackupModal = () => setIsCreateYbaBackupModalOpen(false);
  const openRestoreYbaBackupModal = () => setIsRestoreYbaBackupModalOpen(true);
  const closeRestoreYbaBackupModal = () => setIsRestoreYbaBackupModalOpen(false);

  return (
    <Box display="flex" gridGap={theme.spacing(1)}>
      <RbacValidator accessRequiredOn={ApiPermissionMap.CREATE_ISOLATED_YBA_BACKUP} isControl>
        <YBButton
          variant="secondary"
          onClick={openCreateYbaBackupModal}
          data-testid="ContinuousBackupActionBar-OneTimeExportButton"
        >
          {t('oneTimeBackup')}
        </YBButton>
      </RbacValidator>
      <RbacValidator
        customValidateFunction={() =>
          hasNecessaryPerm(ApiPermissionMap.RESTORE_CONTINUOUS_YBA_BACKUP) ||
          hasNecessaryPerm(ApiPermissionMap.RESTORE_ISOLATED_YBA_BACKUP)
        }
        isControl
      >
        <YBButton
          variant="secondary"
          onClick={openRestoreYbaBackupModal}
          data-testid="ContinuousBackupActionBar-AdvancedRestoreButton"
        >
          {t('advancedRestore')}
        </YBButton>
      </RbacValidator>
      {isCreateYbaBackupModalOpen && (
        <CreateYbaBackupModal
          modalProps={{ open: isCreateYbaBackupModalOpen, onClose: closeCreateYbaBackupModal }}
        />
      )}
      {isRestoreYbaBackupModalOpen && (
        <RestoreYbaBackupModal
          modalProps={{ open: isRestoreYbaBackupModalOpen, onClose: closeRestoreYbaBackupModal }}
        />
      )}
    </Box>
  );
};
