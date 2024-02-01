import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';

import { IStorageConfig as BackupStorageConfig } from '../../../backupv2';

interface ConfigureBootstrapStepProps {
  storageConfigUuid: string;
}

const useStyles = makeStyles((theme) => ({
  formSectionDescription: {
    marginBottom: theme.spacing(3)
  },
  fieldLabel: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',

    marginBottom: theme.spacing(1)
  },
  infoIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  }
}));

const TRANSLATION_KEY_PREFIX =
  'clusterDetail.disasterRecovery.config.editTargetModal.step.configureBootstrap';

export const ConfirmBootstrapStep = ({ storageConfigUuid }: ConfigureBootstrapStepProps) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const storageConfigs: BackupStorageConfig[] = useSelector((reduxState: any) =>
    reduxState?.customer?.configs?.data?.filter(
      (storageConfig: BackupStorageConfig) => storageConfig.type === 'STORAGE'
    )
  );
  const storageConfigName =
    storageConfigs?.find((storageConfig) => storageConfig.configUUID === storageConfigUuid)
      ?.configName ?? '';
  return (
    <>
      <div className={classes.formSectionDescription}>
        <Typography variant="body2">
          <Trans i18nKey={`${TRANSLATION_KEY_PREFIX}.infoText`} components={{ bold: <b /> }} />
        </Typography>
      </div>
      <div className={classes.fieldLabel}>
        <Typography variant="body2">{t('backupStorageConfig.label')}</Typography>
      </div>
      {storageConfigName ? (
        <Typography variant="body2">
          <Trans
            i18nKey={`clusterDetail.disasterRecovery.backupStorageConfig.currentStorageConfigInfo`}
            components={{ bold: <b /> }}
            values={{ storageConfigName: storageConfigName }}
          />
        </Typography>
      ) : (
        <Typography variant="body2">
          {t('missingStorageConfigInfo', {
            keyPrefix: 'clusterDetail.disasterRecovery.backupStorageConfig'
          })}
        </Typography>
      )}
    </>
  );
};
