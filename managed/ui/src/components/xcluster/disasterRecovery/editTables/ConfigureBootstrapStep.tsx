import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';

import { YBTooltip } from '../../../../redesign/components';
import InfoIcon from '../../../../redesign/assets/info-message.svg';
import { IStorageConfig as BackupStorageConfig } from '../../../backupv2';
import { ReactSelectStorageConfigField } from '../../sharedComponents/ReactSelectStorageConfig';
import { DR_DROPDOWN_SELECT_INPUT_WIDTH_PX } from '../constants';
import { useFormContext } from 'react-hook-form';
import { EditTablesFormValues } from './EditTablesModal';

interface ConfigureBootstrapStepProps {
  isDrInterface: boolean;
  isFormDisabled: boolean;

  storageConfigUuid?: string;
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
  'clusterDetail.disasterRecovery.config.editTablesModal.step.configureBootstrap';

export const ConfigureBootstrapStep = ({
  isDrInterface,
  isFormDisabled,
  storageConfigUuid
}: ConfigureBootstrapStepProps) => {
  const { control } = useFormContext<EditTablesFormValues>();
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const storageConfigs: BackupStorageConfig[] = useSelector((reduxState: any) =>
    reduxState?.customer?.configs?.data.filter(
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
          <Trans
            i18nKey={`${TRANSLATION_KEY_PREFIX}.infoText.${isDrInterface ? 'dr' : 'xCluster'}`}
            components={{ bold: <b />, paragraph: <p /> }}
          />
        </Typography>
      </div>
      <div className={classes.fieldLabel}>
        <Typography variant="body2">{t('backupStorageConfig.label')}</Typography>
        {!isDrInterface && (
          <YBTooltip
            title={
              <Typography variant="body2">
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.backupStorageConfig.tooltip`}
                  components={{ paragraph: <p />, bold: <b /> }}
                />
              </Typography>
            }
          >
            <img src={InfoIcon} alt={t('infoIcon', { keyPrefix: 'imgAltText' })} />
          </YBTooltip>
        )}
      </div>
      {/* Backup storage config should already be saved for each DR config. */}
      {isDrInterface ? (
        storageConfigName ? (
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
        )
      ) : (
        <ReactSelectStorageConfigField
          control={control}
          name="storageConfig"
          rules={{ required: t('error.backupStorageConfigRequired') }}
          isDisabled={isFormDisabled}
          autoSizeMinWidth={DR_DROPDOWN_SELECT_INPUT_WIDTH_PX}
          maxWidth="100%"
        />
      )}
    </>
  );
};
