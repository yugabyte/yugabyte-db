import { ReactElement, useState } from 'react';
import { useMutation } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useFieldArray, FieldArrayPath, useForm } from 'react-hook-form';
import { Box, Grid, IconButton, Typography, InputAdornment } from '@material-ui/core';
import { toast } from 'react-toastify';
import {
  YBButton,
  YBCheckbox,
  YBInputField,
  YBModal,
  YBAlert,
  AlertVariant
} from '../../../../../../components';
import { api } from '../../../utils/api';
import {
  AZOverrides,
  Cluster,
  ClusterType,
  HelmOverridesError,
  UniverseConfigure
} from '../../../utils/dto';
import { createErrorMessage } from '../../../utils/helpers';
import { useFormMainStyles } from '../../../universeMainStyle';
//Icons
import { AZ_OVERRIDES_FIELD, UNIVERSE_OVERRIDES_FIELD } from '../../../utils/constants';
import { ReactComponent as CloseIcon } from '../../../../../../assets/close.svg';

interface HelmOverridesModalProps {
  open: boolean;
  onClose: () => void;
  onSubmit: (universeOverrides: string, azOverrides: Record<string, string>) => void;
  universeConfigureTemplate: UniverseConfigure;
  initialValues: OverridesForm;
}

interface OverridesForm {
  azOverrides: AZOverrides[];
  universeOverrides: string;
}

const UNIVERSE_OVERRIDE_SAMPLE = `master:
  podLabels:
    env: test
tserver:
  podLabels:
    env: test`;
const AZ_OVERRIDE_SAMPLE = `us-west-1a:
  tserver:
    podLabels:
      env: test-us-west-1a`;

const INITIAL_VAIDATION_ERRORS: HelmOverridesError = {
  overridesErrors: []
};

export const HelmOverridesModal = ({
  open,
  universeConfigureTemplate,
  initialValues,
  onClose,
  onSubmit
}: HelmOverridesModalProps): ReactElement => {
  const { t } = useTranslation();
  const classes = useFormMainStyles();

  //show helm validation errors from the backend
  const [validationError, setValidationError] = useState<HelmOverridesError>(
    INITIAL_VAIDATION_ERRORS
  );
  const [forceConfirm, setForceConfirm] = useState(false);

  const setOverides = (universeOverrides: string, azOverrides: Record<string, string>) => {
    onSubmit(universeOverrides, azOverrides);
    setValidationError(INITIAL_VAIDATION_ERRORS);
    onClose();
  };

  // validate YAML
  const doValidateYAML = useMutation(
    (values: any) => {
      return api.validateHelmYAML({
        ...values.configurePayload
      });
    },
    {
      onSuccess: (resp, reqValues) => {
        if (resp.overridesErrors.length > 0) {
          // has validation errors
          if (forceConfirm) {
            //apply overrides, close modal and clear error
            setOverides(reqValues.values.universeOverrides, reqValues.values.azOverrides);
          } else {
            setValidationError(resp);
          }
        } else {
          //apply overrides, close modal and clear error
          setOverides(reqValues.values.universeOverrides, reqValues.values.azOverrides);
        }
      },
      onError: (err: any, reqValues) => {
        // sometimes, the backend throws 500 error, if the validation is failed. we don't want to block the user if that happens
        if (err.response.status === 500) {
          setOverides(reqValues.values.universeOverrides, reqValues.values.azOverrides);
        } else {
          // user can still use force apply option to apply overrides
          toast.error(createErrorMessage(err));
        }
      }
    }
  );

  const INITIAL_FORM_VALUE = {
    ...initialValues,
    azOverrides: Object.keys(initialValues.azOverrides).map((k) => ({
      value: k + `:\n${initialValues.azOverrides[k]}`
    }))
  };

  const { control, getValues } = useForm<OverridesForm>({
    mode: 'onChange',
    defaultValues: INITIAL_FORM_VALUE
  });
  const { fields, append, remove } = useFieldArray({
    control,
    name: AZ_OVERRIDES_FIELD
  });

  const handleSave = () => {
    const formValues = getValues();

    const universeOverrides = formValues.universeOverrides;
    const azOverrides = formValues?.azOverrides
      ? formValues.azOverrides.reduce((ovverridesObj, { value }) => {
          if (value) {
            const regionIndex = value.indexOf('\n');
            const region = value.substring(0, regionIndex).trim().replace(':', '');
            const regionOverride = value.substring(regionIndex + 1);
            ovverridesObj[region] = regionOverride;
          }
          return ovverridesObj;
        }, {})
      : {};

    const configurePayload: UniverseConfigure = {
      ...universeConfigureTemplate,
      clusters: universeConfigureTemplate?.clusters?.map((cluster: Cluster) => {
        if (cluster.clusterType === ClusterType.PRIMARY) {
          //Attach universe ovverides
          if (universeOverrides) cluster.userIntent.universeOverrides = universeOverrides;

          //Attach AZ ovverides
          cluster.userIntent.azOverrides = azOverrides;
        }

        return cluster;
      })
    };

    if (forceConfirm) {
      setOverides(universeOverrides, azOverrides);
    } else {
      doValidateYAML.mutateAsync({
        configurePayload,
        values: { universeOverrides, azOverrides }
      });
    }
  };

  return (
    <YBModal
      open={open}
      size="lg"
      title={t('universeForm.helmOverrides.k8sOverrides')}
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.validateAndSave')}
      cancelTestId="HelmOverridesModal-CancelButton"
      submitTestId="HelmOverridesModal-SubmitButton"
      onClose={onClose}
      titleSeparator
      onSubmit={handleSave}
      footerAccessory={
        <YBCheckbox
          checked={forceConfirm}
          onChange={() => setForceConfirm(!forceConfirm)}
          label={t('universeForm.helmOverrides.forceApply')}
          inputProps={{
            'data-testid': 'HelmOverridesModal-ForceConfirm'
          }}
        />
      }
    >
      {!!validationError.overridesErrors.length && (
        <Box my={2}>
          <YBAlert
            icon={<></>}
            open={true}
            text={
              <Box my={1}>
                <Box>
                  <Typography variant="body1">
                    {t('universeForm.helmOverrides.yamlErrorsTitle')}:
                  </Typography>
                </Box>
                <Box>
                  {validationError.overridesErrors.map((error, index) => (
                    <Box mt={0.5}>
                      <Typography variant="body2">
                        {index + 1}. {error.errorString}.
                      </Typography>
                    </Box>
                  ))}
                </Box>
              </Box>
            }
            variant={AlertVariant.Error}
          />
        </Box>
      )}
      <Box>
        <Box>
          <Typography variant="h5">{t('universeForm.helmOverrides.universeOverrides')}:</Typography>
        </Box>
        <YBInputField
          name={UNIVERSE_OVERRIDES_FIELD}
          control={control}
          fullWidth
          multiline
          minRows={8}
          maxRows={8}
          placeholder={UNIVERSE_OVERRIDE_SAMPLE}
          inputProps={{
            'data-testid': 'HelmOverridesModal-UniverseOverrideInput'
          }}
        />
      </Box>
      <Grid container direction="column">
        <Box display="flex" flexDirection="column" mt={2}>
          <Box>
            <Typography variant="h5">
              {t('universeForm.helmOverrides.availabilityZone')}:
            </Typography>
          </Box>
          {fields.map((field, index) => {
            return (
              <Box mt={1} key={field.id}>
                <Box>
                  <Typography variant="h6">
                    {t('universeForm.helmOverrides.azOverrides')} {index + 1}:
                  </Typography>
                </Box>
                <YBInputField
                  name={`${AZ_OVERRIDES_FIELD}.${index}.value` as FieldArrayPath<string[]>}
                  control={control}
                  fullWidth
                  multiline
                  minRows={5}
                  maxRows={5}
                  placeholder={AZ_OVERRIDE_SAMPLE}
                  inputProps={{
                    'data-testid': `HelmOverridesModal-UniverseOverrideInput${index}`
                  }}
                  InputProps={{
                    endAdornment: (
                      <InputAdornment position="end">
                        <IconButton
                          onClick={() => remove(index)}
                          tabIndex="-1"
                          data-testid={`HelmOverridesModal-InputClose${index}`}
                        >
                          <CloseIcon />
                        </IconButton>
                      </InputAdornment>
                    )
                  }}
                />
              </Box>
            );
          })}
        </Box>
        <Box mt={2}>
          <YBButton
            className={classes.formButtons}
            variant="primary"
            data-testid={`HelmOverridesModal-AddAZButton`}
            onClick={() => append({ value: '' })}
          >
            <span className="fa fa-plus" />
            {t('universeForm.helmOverrides.addAvailabilityZone')}
          </YBButton>
        </Box>
      </Grid>
    </YBModal>
  );
};
