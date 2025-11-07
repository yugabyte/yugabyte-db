import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { AxiosError } from 'axios';
import { Box, useTheme } from '@material-ui/core';

import { YBLoading } from '../../../components/common/indicators';
import YBErrorIndicator from '../../../components/common/indicators/YBErrorIndicator';
import { getContinuousBackup } from '../../../v2/api/continuous-backup/continuous-backup';
import { CONTINUOUS_BACKUP_QUERY_KEY } from '../../helpers/api';
import {
  ConfigureContinuousBackupModal,
  ConfigureContinuousBackupOperation
} from './ConfigureContinuousBackupModal';
import { ContinuousBackupCard } from './ContinuousBackupCard';
import { EnableContinuousBackupPrompt } from './EnableContinuousBackupPrompt';
import { ContinuousBackupActionBar } from './ContinuousBackupActionBar';

const TRANSLATION_KEY_PREFIX = 'clusterDetail.continuousBackup.enableContinuousBackupPrompt';

export const ContinuousBackup = () => {
  const [isConfigureContinuousBackupModalOpen, setIsConfigureContinuousBackupModalOpen] = useState(
    false
  );
  const theme = useTheme();

  const continuousBackupConfigQuery = useQuery(CONTINUOUS_BACKUP_QUERY_KEY, () =>
    getContinuousBackup()
  );
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });

  if (continuousBackupConfigQuery.isLoading || continuousBackupConfigQuery.isIdle) {
    return <YBLoading />;
  }

  const openConfigureContinuousBackupModal = () => setIsConfigureContinuousBackupModalOpen(true);
  const closeConfigureContinuousBackupModal = () => setIsConfigureContinuousBackupModalOpen(false);

  // Check if the error is a 404 (not configured) vs other errors
  const isContinuousBackupNotConfigured =
    continuousBackupConfigQuery.isError &&
    (continuousBackupConfigQuery.error as AxiosError)?.response?.status === 404;
  if (continuousBackupConfigQuery.isError && !isContinuousBackupNotConfigured) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchContinuousBackupConfig', { keyPrefix: 'queryError' })}
      />
    );
  }

  return (
    <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
      <Box marginLeft="auto">
        <ContinuousBackupActionBar />
      </Box>
      {isContinuousBackupNotConfigured ? (
        <>
          <EnableContinuousBackupPrompt
            isDisabled={continuousBackupConfigQuery.isLoading}
            onEnableContinuousBackupClick={openConfigureContinuousBackupModal}
          />
          <ConfigureContinuousBackupModal
            operation={ConfigureContinuousBackupOperation.CREATE}
            modalProps={{
              open: isConfigureContinuousBackupModalOpen,
              onClose: closeConfigureContinuousBackupModal
            }}
          />
        </>
      ) : (
        continuousBackupConfigQuery.data && (
          <ContinuousBackupCard continuousBackupConfig={continuousBackupConfigQuery.data} />
        )
      )}
    </Box>
  );
};
