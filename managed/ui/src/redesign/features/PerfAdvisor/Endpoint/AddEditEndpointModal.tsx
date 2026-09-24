import { Box, MenuItem } from '@material-ui/core';
import { FormProvider, useForm, useWatch } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import {
  YBInputField,
  YBLabel,
  YBModal,
  YBPasswordField,
  YBSelectField
} from '../../../components';
import {
  useCreatePerfAdvisorEndpoint,
  useEditPerfAdvisorEndpoint,
  getListPerfAdvisorEndpointsQueryKey
} from '../../../../v2/api/perf-advisor-endpoint/perf-advisor-endpoint';
import { createErrorMessage } from '../../universe/universe-form/utils/helpers';
import {
  PerfAdvisorEndpoint,
  PerfAdvisorEndpointAuthType,
  PerfAdvisorEndpointMetricsType,
  PerfAdvisorEndpointSpec,
  PerfAdvisorEndpointType
} from '../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

interface AddEditEndpointModalProps {
  open: boolean;
  /** The endpoint being edited, or null when adding one. */
  endpoint: PerfAdvisorEndpoint | null;
  onClose: () => void;
}

interface FormValues {
  name: string;
  type: PerfAdvisorEndpointType;
  collectionEndpoint: string;
  collectionAuthType: PerfAdvisorEndpointAuthType;
  collectionUsername: string;
  collectionPassword: string;
  metricsEndpoint: string;
  metricsType: PerfAdvisorEndpointMetricsType;
  metricsAuthType: PerfAdvisorEndpointAuthType;
  metricsUsername: string;
  metricsPassword: string;
  ybmAccountId: string;
  ybmProjectId: string;
}

const I18N = 'clusterDetail.perfAdvisorEndpoint';

/**
 * The server reports errors against its own field names, which match the form's except where it
 * nests credentials under an auth object.
 */
const SERVER_FIELD_TO_FORM_FIELD: Record<string, keyof FormValues> = {
  'metricsAuth.username': 'metricsUsername',
  'metricsAuth.password': 'metricsPassword',
  'collectionAuth.username': 'collectionUsername',
  'collectionAuth.password': 'collectionPassword'
};

/** Guards against setting an error on a field the form does not have. */
const EMPTY_FORM_VALUES: Record<keyof FormValues, true> = {
  name: true,
  type: true,
  collectionEndpoint: true,
  collectionAuthType: true,
  collectionUsername: true,
  collectionPassword: true,
  metricsEndpoint: true,
  metricsType: true,
  metricsAuthType: true,
  metricsUsername: true,
  metricsPassword: true,
  ybmAccountId: true,
  ybmProjectId: true
};

const toFormValues = (endpoint: PerfAdvisorEndpoint | null): FormValues => {
  const spec = endpoint?.spec;
  return {
    name: spec?.name ?? '',
    type: spec?.type ?? 'BYOC',
    collectionEndpoint: spec?.collection_endpoint ?? '',
    collectionAuthType: spec?.collection_auth?.type ?? 'NONE',
    collectionUsername: spec?.collection_auth?.username ?? '',
    collectionPassword: spec?.collection_auth?.password ?? '',
    metricsEndpoint: spec?.metrics_endpoint ?? '',
    metricsType: spec?.metrics_type ?? 'otlphttp',
    metricsAuthType: spec?.metrics_auth?.type ?? 'NONE',
    metricsUsername: spec?.metrics_auth?.username ?? '',
    metricsPassword: spec?.metrics_auth?.password ?? '',
    ybmAccountId: spec?.ybm_account_id ?? '',
    ybmProjectId: spec?.ybm_project_id ?? ''
  };
};

export const AddEditEndpointModal = ({ open, endpoint, onClose }: AddEditEndpointModalProps) => {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const endpointUuid = endpoint?.info?.uuid;
  const isEdit = !!endpointUuid;

  const formMethods = useForm<FormValues>({
    defaultValues: toFormValues(endpoint),
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const { control, handleSubmit, setError } = formMethods;

  const collectionAuthType = useWatch({ control, name: 'collectionAuthType' });
  const metricsAuthType = useWatch({ control, name: 'metricsAuthType' });

  const onSuccess = () => {
    queryClient.invalidateQueries(getListPerfAdvisorEndpointsQueryKey());
    toast.success(isEdit ? t(`${I18N}.updated`) : t(`${I18N}.created`));
    onClose();
  };

  const onError = (e: any) => {
    // Saving probes the destination, so the interesting failures name a field and say what is wrong
    // with it - an unreachable URL, a rejected credential, a protocol that does not match the path.
    // The response carries them as a field-to-messages object, which is what createErrorMessage
    // flattens; attaching them to the inputs as well puts each message where its field is.
    const structuredError = e?.response?.data?.error;
    if (structuredError && typeof structuredError === 'object') {
      Object.entries(structuredError).forEach(([field, messages]) => {
        const formField = SERVER_FIELD_TO_FORM_FIELD[field] ?? field;
        if (formField in EMPTY_FORM_VALUES) {
          setError(formField as keyof FormValues, {
            type: 'server',
            message: Array.isArray(messages) ? messages.join(', ') : String(messages)
          });
        }
      });
    }
    toast.error(createErrorMessage(e) ?? t(`${I18N}.saveFailed`));
  };

  const createEndpoint = useCreatePerfAdvisorEndpoint({ mutation: { onSuccess, onError } });
  const editEndpoint = useEditPerfAdvisorEndpoint({ mutation: { onSuccess, onError } });

  const handleFormSubmit = handleSubmit((values) => {
    const spec: PerfAdvisorEndpointSpec = {
      name: values.name.trim(),
      type: values.type,
      collection_endpoint: values.collectionEndpoint.trim(),
      collection_auth:
        values.collectionAuthType === 'BASIC'
          ? {
              type: 'BASIC',
              username: values.collectionUsername,
              password: values.collectionPassword
            }
          : { type: 'NONE' },
      metrics_endpoint: values.metricsEndpoint.trim(),
      metrics_type: values.metricsType,
      metrics_auth:
        values.metricsAuthType === 'BASIC'
          ? { type: 'BASIC', username: values.metricsUsername, password: values.metricsPassword }
          : { type: 'NONE' },
      ybm_account_id: values.ybmAccountId.trim() || undefined,
      ybm_project_id: values.ybmProjectId.trim() || undefined
    };
    if (isEdit) {
      editEndpoint.mutate({ peUUID: endpointUuid!, data: spec });
    } else {
      createEndpoint.mutate({ data: spec });
    }
  });

  const required = { required: t(`${I18N}.fieldRequired`) };
  const isSubmitting = createEndpoint.isLoading || editEndpoint.isLoading;

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={isEdit ? t(`${I18N}.editTitle`) : t(`${I18N}.addTitle`)}
      onSubmit={handleFormSubmit}
      cancelLabel={t('common.cancel')}
      submitLabel={isEdit ? t('common.applyChanges') : t('common.add')}
      isSubmitting={isSubmitting}
      size="md"
      titleSeparator
      enableBackdropDismiss
      submitTestId="AddEditEndpointModal-Submit"
      cancelTestId="AddEditEndpointModal-Cancel"
    >
      <FormProvider {...formMethods}>
        <Box
          display="flex"
          flexDirection="column"
          gridGap={16}
          pt={2}
          pb={2}
          data-testid="AddEditEndpointModal-Container"
        >
          <Box display="flex" flexDirection="row" alignItems="center">
            <YBLabel width="220px" dataTestId="Endpoint-Name-Label">
              {t(`${I18N}.name`)}
            </YBLabel>
            <Box flex={1}>
              <YBInputField control={control} name="name" fullWidth rules={required} />
            </Box>
          </Box>

          <Box display="flex" flexDirection="row" alignItems="center">
            <YBLabel width="220px" dataTestId="Endpoint-Type-Label">
              {t(`${I18N}.type`)}
            </YBLabel>
            <Box flex={1}>
              {/* BYOC is the only kind that exists so far; the field is here because the API models
                  it and a second kind is coming. */}
              <YBSelectField control={control} name="type" fullWidth>
                <MenuItem value="BYOC">{t(`${I18N}.typeByoc`)}</MenuItem>
              </YBSelectField>
            </Box>
          </Box>

          <Box display="flex" flexDirection="row" alignItems="center">
            <YBLabel width="220px" dataTestId="Endpoint-CollectionEndpoint-Label">
              {t(`${I18N}.collectionEndpoint`)}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="collectionEndpoint"
                fullWidth
                placeholder="https://perf-advisor.example.com:9443"
                rules={required}
              />
            </Box>
          </Box>

          <Box display="flex" flexDirection="row" alignItems="center">
            <YBLabel width="220px" dataTestId="Endpoint-CollectionAuth-Label">
              {t(`${I18N}.collectionAuth`)}
            </YBLabel>
            <Box flex={1}>
              <YBSelectField control={control} name="collectionAuthType" fullWidth>
                <MenuItem value="NONE">{t(`${I18N}.authNone`)}</MenuItem>
                <MenuItem value="BASIC">{t(`${I18N}.authBasic`)}</MenuItem>
              </YBSelectField>
            </Box>
          </Box>

          {collectionAuthType === 'BASIC' && (
            <>
              <Box display="flex" flexDirection="row" alignItems="center">
                <YBLabel width="220px" dataTestId="Endpoint-CollectionUsername-Label">
                  {t(`${I18N}.username`)}
                </YBLabel>
                <Box flex={1}>
                  <YBInputField
                    control={control}
                    name="collectionUsername"
                    fullWidth
                    rules={required}
                  />
                </Box>
              </Box>
              <Box display="flex" flexDirection="row" alignItems="center">
                <YBLabel width="220px" dataTestId="Endpoint-CollectionPassword-Label">
                  {t(`${I18N}.password`)}
                </YBLabel>
                <Box flex={1}>
                  <YBPasswordField
                    control={control}
                    name="collectionPassword"
                    fullWidth
                    rules={required}
                  />
                </Box>
              </Box>
            </>
          )}

          <Box display="flex" flexDirection="row" alignItems="center">
            <YBLabel width="220px" dataTestId="Endpoint-MetricsEndpoint-Label">
              {t(`${I18N}.metricsEndpoint`)}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="metricsEndpoint"
                fullWidth
                placeholder="https://perf-advisor.example.com/api/v1/otlp/metrics"
                rules={required}
              />
            </Box>
          </Box>

          <Box display="flex" flexDirection="row" alignItems="center">
            <YBLabel width="220px" dataTestId="Endpoint-MetricsType-Label">
              {t(`${I18N}.metricsType`)}
            </YBLabel>
            <Box flex={1}>
              <YBSelectField control={control} name="metricsType" fullWidth>
                <MenuItem value="otlphttp">{t(`${I18N}.metricsTypeOtlp`)}</MenuItem>
                <MenuItem value="remotewrite">{t(`${I18N}.metricsTypeRemoteWrite`)}</MenuItem>
              </YBSelectField>
            </Box>
          </Box>

          <Box display="flex" flexDirection="row" alignItems="center">
            <YBLabel width="220px" dataTestId="Endpoint-MetricsAuth-Label">
              {t(`${I18N}.metricsAuth`)}
            </YBLabel>
            <Box flex={1}>
              <YBSelectField control={control} name="metricsAuthType" fullWidth>
                <MenuItem value="NONE">{t(`${I18N}.authNone`)}</MenuItem>
                <MenuItem value="BASIC">{t(`${I18N}.authBasic`)}</MenuItem>
              </YBSelectField>
            </Box>
          </Box>

          {metricsAuthType === 'BASIC' && (
            <>
              <Box display="flex" flexDirection="row" alignItems="center">
                <YBLabel width="220px" dataTestId="Endpoint-MetricsUsername-Label">
                  {t(`${I18N}.username`)}
                </YBLabel>
                <Box flex={1}>
                  <YBInputField
                    control={control}
                    name="metricsUsername"
                    fullWidth
                    rules={required}
                  />
                </Box>
              </Box>
              <Box display="flex" flexDirection="row" alignItems="center">
                <YBLabel width="220px" dataTestId="Endpoint-MetricsPassword-Label">
                  {t(`${I18N}.password`)}
                </YBLabel>
                <Box flex={1}>
                  <YBPasswordField
                    control={control}
                    name="metricsPassword"
                    fullWidth
                    rules={required}
                  />
                </Box>
              </Box>
            </>
          )}

          <Box display="flex" flexDirection="row" alignItems="center">
            <YBLabel width="220px" dataTestId="Endpoint-YbmAccountId-Label">
              {t(`${I18N}.ybmAccountId`)}
            </YBLabel>
            <Box flex={1}>
              <YBInputField control={control} name="ybmAccountId" fullWidth />
            </Box>
          </Box>

          <Box display="flex" flexDirection="row" alignItems="center">
            <YBLabel width="220px" dataTestId="Endpoint-YbmProjectId-Label">
              {t(`${I18N}.ybmProjectId`)}
            </YBLabel>
            <Box flex={1}>
              <YBInputField control={control} name="ybmProjectId" fullWidth />
            </Box>
          </Box>
        </Box>
      </FormProvider>
    </YBModal>
  );
};
