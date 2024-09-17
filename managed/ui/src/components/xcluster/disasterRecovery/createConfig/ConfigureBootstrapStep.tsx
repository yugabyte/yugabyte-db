import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';

import InfoIcon from '../../../../redesign/assets/info-message.svg';
import { CreateDrConfigFormValues } from './CreateConfigModal';
import { INPUT_FIELD_WIDTH_PX } from '../../constants';
import { ReactSelectStorageConfigField } from '../../sharedComponents/ReactSelectStorageConfig';
import { YBTooltip } from '../../../../redesign/components';

import { useModalStyles } from '../../styles';

interface ConfigureBootstrapStepProps {
  isFormDisabled: boolean;
}

const TRANSLATION_KEY_PREFIX =
  'clusterDetail.disasterRecovery.config.createModal.step.configureBootstrap';

export const ConfigureBootstrapStep = ({ isFormDisabled }: ConfigureBootstrapStepProps) => {
  const { control } = useFormContext<CreateDrConfigFormValues>();
  const classes = useModalStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  return (
    <div className={classes.stepContainer}>
      <ol start={3}>
        <li>
          <Typography variant="body1" className={classes.instruction}>
            {t('instruction')}
          </Typography>
          <div className={classes.formSectionDescription}>
            <Typography variant="body2">{t('infoText')}</Typography>
          </div>
          <div className={classes.fieldLabel}>
            <Typography variant="body2">{t('backupStorageConfig.label')}</Typography>
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
          </div>
          <ReactSelectStorageConfigField
            control={control}
            name="storageConfig"
            rules={{ required: t('error.backupStorageConfigRequired') }}
            isDisabled={isFormDisabled}
            autoSizeMinWidth={INPUT_FIELD_WIDTH_PX}
            maxWidth="100%"
          />
        </li>
      </ol>
    </div>
  );
};
