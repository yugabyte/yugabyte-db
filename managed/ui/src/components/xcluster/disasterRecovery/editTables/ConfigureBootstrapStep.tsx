import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';

import { YBTooltip } from '../../../../redesign/components';
import { ReactComponent as InfoIcon } from '../../../../redesign/assets/info-message.svg';
import { ReactSelectStorageConfigField } from '../../sharedComponents/ReactSelectStorageConfig';
import { EditTablesFormValues } from './EditTablesModal';
import { DR_DROPDOWN_SELECT_INPUT_WIDTH_PX } from '../constants';

interface ConfigureBootstrapStepProps {
  isFormDisabled: boolean;
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

export const ConfigureBootstrapStep = ({ isFormDisabled }: ConfigureBootstrapStepProps) => {
  const { control } = useFormContext<EditTablesFormValues>();
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  return (
    <>
      <div className={classes.formSectionDescription}>
        <Typography variant="body2">
          <Trans
            i18nKey={`${TRANSLATION_KEY_PREFIX}.infoText`}
            components={{ bold: <b />, paragraph: <p /> }}
          />
        </Typography>
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
          <InfoIcon className={classes.infoIcon} />
        </YBTooltip>
      </div>
      <ReactSelectStorageConfigField
        control={control}
        name="storageConfig"
        rules={{ required: t('error.backupStorageConfigRequired') }}
        isDisabled={isFormDisabled}
        autoSizeMinWidth={DR_DROPDOWN_SELECT_INPUT_WIDTH_PX}
        maxWidth="100%"
      />
    </>
  );
};
