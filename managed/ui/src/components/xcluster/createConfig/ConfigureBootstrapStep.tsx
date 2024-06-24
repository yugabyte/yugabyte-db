import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';

import { YBTooltip } from '../../../redesign/components';
import InfoIcon from '../../../redesign/assets/info-message.svg';
import { ReactSelectStorageConfigField } from '../sharedComponents/ReactSelectStorageConfig';
import { CreateXClusterConfigFormValues } from './CreateConfigModal';

interface ConfigureBootstrapStepProps {
  isFormDisabled: boolean;
}

const useStyles = makeStyles((theme) => ({
  stepContainer: {
    '& ol': {
      paddingLeft: theme.spacing(2),
      listStylePosition: 'outside',
      '& li::marker': {
        fontWeight: 'bold'
      }
    }
  },
  instruction: {
    marginBottom: theme.spacing(4)
  },
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

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.createConfigModal.step.configureBootstrap';

export const ConfigureBootstrapStep = ({ isFormDisabled }: ConfigureBootstrapStepProps) => {
  const { control } = useFormContext<CreateXClusterConfigFormValues>();
  const classes = useStyles();
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
            autoSizeMinWidth={350}
            maxWidth="100%"
          />
        </li>
      </ol>
    </div>
  );
};
