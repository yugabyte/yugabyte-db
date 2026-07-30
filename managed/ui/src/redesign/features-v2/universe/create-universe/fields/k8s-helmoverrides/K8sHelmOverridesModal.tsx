import { ReactElement, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useFieldArray, FieldArrayPath, useForm } from 'react-hook-form';
import { Box, Grid, IconButton, InputAdornment } from '@material-ui/core';
import { toast } from 'react-toastify';
import {
  YBInputField,
  YBCheckbox,
  YBAlert,
  AlertVariant,
  yba,
  YBButton,
  mui
} from '@yugabyte-ui-library/core';
import {
  AZOverrides,
  K8sHelmOverridesError
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { createErrorMessage } from '@app/redesign/features/universe/universe-form/utils/helpers';
import {
  ClusterPlacementSpec,
  UniverseValidateKubernetesOverridesReqBody
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { useValidateKubernetesOverrides } from '@app/v2/api/universe/universe';
import { AZ_OVERRIDES_FIELD, UNIVERSE_OVERRIDES_FIELD } from '../FieldNames';
//Icons
import CloseIcon from '@app/redesign/assets/close.svg';
import CircleAddIcon from '@app/redesign/assets/circle-add-v2.svg';

const { YBModal } = yba;
const { Typography } = mui;

interface HelmOverridesModalProps {
  open: boolean;
  onClose: () => void;
  onSubmit: (universeOverrides: string, azOverrides: Record<string, string>) => void;
  initialValues: OverridesForm;
  placementSpec?: ClusterPlacementSpec;
  dbVersion?: string;
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

const INITIAL_VAIDATION_ERRORS: K8sHelmOverridesError = {
  errors: []
};

export const K8sHelmOverridesModal = ({
  open,
  initialValues,
  onClose,
  onSubmit,
  placementSpec,
  dbVersion
}: HelmOverridesModalProps): ReactElement => {
  const { t } = useTranslation();
  const validateOverrides = useValidateKubernetesOverrides();
  //show helm validation errors from the backend
  const [validationError, setValidationError] =
    useState<K8sHelmOverridesError>(INITIAL_VAIDATION_ERRORS);
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const [forceConfirm, setForceConfirm] = useState(false);

  const setOverides = (universeOverrides: string, azOverrides: Record<string, string>) => {
    onSubmit(universeOverrides, azOverrides);
    setValidationError(INITIAL_VAIDATION_ERRORS);
    onClose();
  };

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
    !forceConfirm && setIsSubmitting(true);

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

    if (forceConfirm) {
      setOverides(universeOverrides, azOverrides);
    } else {
      const cUUID = localStorage.getItem('customerId') ?? '';
      const payload: UniverseValidateKubernetesOverridesReqBody = {
        yb_software_version: dbVersion,
        placement_spec: placementSpec,
        overrides: universeOverrides,
        az_overrides: azOverrides,
        is_readonly_cluster: false,
        node_prefix: ''
      };

      validateOverrides.mutate(
        {
          data: payload,
          cUUID
        },
        {
          onSuccess: (resp) => {
            if (resp?.errors?.length > 0) {
              // has validation errors
              if (forceConfirm) {
                //apply overrides, close modal and clear error
                setOverides(universeOverrides, azOverrides);
              } else {
                setValidationError(resp);
              }
            } else {
              //apply overrides, close modal and clear error
              setOverides(universeOverrides, azOverrides);
            }
            setIsSubmitting(false);
            toast.success(t('universeForm.helmOverrides.validateAndSaveSuccess'));
          },
          onError: (err: any) => {
            // sometimes, the backend throws 500 error, if the validation is failed. we don't want to block the user if that happens
            if (err.response.status === 500) {
              setOverides(universeOverrides, azOverrides);
            } else {
              // user can still use force apply option to apply overrides
              toast.error(createErrorMessage(err));
            }
            setIsSubmitting(false);
          }
        }
      );
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
      buttonProps={{
        primary: {
          disabled: isSubmitting,
          dataTestId: 'HelmOverridesModal-SubmitButton'
        }
      }}
      actionsInfo={
        <YBCheckbox
          checked={forceConfirm}
          onChange={() => setForceConfirm(!forceConfirm)}
          label={t('universeForm.helmOverrides.forceApply')}
          dataTestId="HelmOverridesModal-ForceConfirm"
        />
      }
    >
      {!!validationError.errors.length && (
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
                  {validationError.errors.map((error, index) => (
                    <Box mt={0.5}>
                      <Typography variant="body2">
                        {index + 1}. {error}.
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
      <Box
        display="flex"
        p={3}
        flexDirection={'column'}
        border="1px solid #D7DEE4"
        bgcolor={'#FBFCFD'}
        borderRadius={'8px'}
      >
        <Box mb={2}>
          <Typography variant="h5" sx={{ fontWeight: 600 }}>
            {t('universeForm.helmOverrides.universeOverrides')}:
          </Typography>
        </Box>
        <YBInputField
          name={UNIVERSE_OVERRIDES_FIELD}
          control={control}
          fullWidth
          multiline
          minRows={8}
          maxRows={8}
          placeholder={UNIVERSE_OVERRIDE_SAMPLE}
          label={'Overrides'}
          dataTestId="HelmOverridesModal-UniverseOverrideInput"
          inputProps={{
            'data-testid': 'HelmOverridesModal-UniverseOverrideInput'
          }}
        />
      </Box>
      <Grid container direction="column">
        <Box
          display="flex"
          flexDirection="column"
          mt={2}
          p={3}
          border="1px solid #D7DEE4"
          bgcolor={'#FBFCFD'}
          borderRadius={'8px'}
        >
          <Box>
            <Typography variant="h5" sx={{ fontWeight: 600 }}>
              {t('universeForm.helmOverrides.availabilityZone')}:
            </Typography>
          </Box>
          {fields.map((field, index) => {
            return (
              <Box mt={3} key={field.id}>
                <YBInputField
                  name={`${AZ_OVERRIDES_FIELD}.${index}.value` as FieldArrayPath<string[]>}
                  control={control}
                  fullWidth
                  multiline
                  minRows={5}
                  maxRows={5}
                  placeholder={AZ_OVERRIDE_SAMPLE}
                  dataTestId={`HelmOverridesModal-UniverseOverrideInput${index}`}
                  label={`${t('universeForm.helmOverrides.azOverrides')} ${index + 1}:`}
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
          <Box mt={2}>
            <YBButton
              variant="secondary"
              startIcon={<CircleAddIcon />}
              dataTestId={`HelmOverridesModal-AddAZButton`}
              sx={{ width: 'fit-content' }}
              onClick={() => append({ value: '' })}
            >
              {t('universeForm.helmOverrides.addAvailabilityZone')}
            </YBButton>
          </Box>
        </Box>
      </Grid>
    </YBModal>
  );
};
