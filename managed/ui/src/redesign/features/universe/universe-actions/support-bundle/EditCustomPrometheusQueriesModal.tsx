import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { SubmitHandler, useFieldArray, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';

import { YBButton, YBInputField, YBModal, YBModalProps } from '../../../../components';
import DeleteIcon from '../../../../assets/delete2.svg';
import AddIcon from '../../../../assets/add2.svg';

interface CustomPrometheusQueries {
  folderName: string;
  query: string;
}

interface EditCustomPrometheusQueriesModalProps {
  customPrometheusQueries: CustomPrometheusQueries[];
  updateCustomPrometheusQueries: (customPrometheusQueries: CustomPrometheusQueries[]) => void;
  modalProps: YBModalProps;
}

interface EditCustomPrometheusQueriesFormValue {
  customPrometheusQueries: CustomPrometheusQueries[];
}

const useStyles = makeStyles((theme) => ({
  prometheusQueryRow: {
    display: 'flex',
    gap: theme.spacing(1),

    padding: theme.spacing(2),

    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: '8px'
  },
  removeQueryButton: {
    color: '#151730'
  },
  addQueryButton: {
    width: 'fit-content'
  },
  inputFieldComponent: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(0.5)
  }
}));

const MODAL_NAME = 'EditCustomPrometheusQueriesModal';
const TRANSLATION_KEY_PREFIX = 'universeActions.supportBundle.editCustomPrometheusQueriesModal';
export const EditCustomPrometheusQueriesModal = ({
  customPrometheusQueries,
  updateCustomPrometheusQueries,
  modalProps
}: EditCustomPrometheusQueriesModalProps) => {
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const { control, handleSubmit, getValues, trigger } = useForm<
    EditCustomPrometheusQueriesFormValue
  >({
    defaultValues: {
      customPrometheusQueries
    },
    mode: 'onBlur'
  });

  const { fields: prometheusQueries, append, remove } = useFieldArray({
    control,
    name: 'customPrometheusQueries'
  });

  const onSubmit: SubmitHandler<EditCustomPrometheusQueriesFormValue> = (formValues) => {
    updateCustomPrometheusQueries(formValues.customPrometheusQueries);
    modalProps.onClose();
  };

  const addCustomQuery = () => {
    append({ folderName: '', query: '' });
  };

  return (
    <YBModal
      title={t('title')}
      submitLabel={t('submitLabel')}
      onSubmit={handleSubmit(onSubmit)}
      size="md"
      overrideHeight="fit-content"
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      {...modalProps}
    >
      <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
        <Typography variant="body2">{t('infoText')}</Typography>
        <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
          {prometheusQueries.map((prometheusQuery, index) => {
            return (
              <div className={classes.prometheusQueryRow} key={prometheusQuery.id}>
                <Box display="flex" flexDirection="column" gridGap={theme.spacing(1.5)} flex={1}>
                  <Typography variant="body1">{t('customQuery')}</Typography>
                  <div className={classes.inputFieldComponent}>
                    <Typography variant="body2">{t('folderName')}</Typography>
                    <YBInputField
                      control={control}
                      name={`customPrometheusQueries.${index}.folderName`}
                      fullWidth
                      onBlur={() => trigger('customPrometheusQueries')}
                      rules={{
                        required: t('formFieldRequired', { keyPrefix: 'common' }),
                        pattern: {
                          value: /^[^#%&{}\<>*?/!'"@:+=`|$]*$/,
                          message: t('validationError.folderNameHasSpecialCharacters')
                        },
                        validate: (value) => {
                          const existingPrometheusQueries = getValues('customPrometheusQueries');
                          return existingPrometheusQueries.find(
                            (existingPrometheusQuery, comparisonIndex) =>
                              comparisonIndex !== index &&
                              existingPrometheusQuery.folderName === value
                          )
                            ? t('validationError.folderNameMustBeUnique')
                            : true;
                        }
                      }}
                    />
                  </div>
                  <div className={classes.inputFieldComponent}>
                    <Typography variant="body2">{t('prometheusQuery')}</Typography>
                    <YBInputField
                      control={control}
                      name={`customPrometheusQueries.${index}.query`}
                      rules={{
                        required: t('formFieldRequired', { keyPrefix: 'common' })
                      }}
                      fullWidth
                    />
                  </div>
                </Box>
                <Box justifyContent="top">
                  <YBButton
                    className={classes.removeQueryButton}
                    variant="ghost"
                    startIcon={<DeleteIcon />}
                    onClick={() => remove(index)}
                    data-testid={`${MODAL_NAME}-PrometheusQuery${index}-DeleteButton`}
                  />
                </Box>
              </div>
            );
          })}
        </Box>
        <YBButton
          className={classes.addQueryButton}
          variant="secondary"
          startIcon={<AddIcon />}
          onClick={addCustomQuery}
          data-testid={`${MODAL_NAME}-AddCustomQuery`}
          size="large"
        >
          {t('addCustomQuery')}
        </YBButton>
      </Box>
    </YBModal>
  );
};
