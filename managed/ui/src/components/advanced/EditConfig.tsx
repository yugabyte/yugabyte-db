import { FC, useRef } from 'react';
import { Row, Col } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { Field, FieldProps, FormikProps } from 'formik';
import { Typography } from '@material-ui/core';

import { YBModalForm } from '../common/forms';
import { YBFormInput, YBToggle } from '../common/forms/fields';
import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '../../actions/customers';
import { RunTimeConfigData, RunTimeConfigScope } from '../../redesign/utils/dtos';
import { isEmptyObject } from '../../utils/ObjectUtils';
import { RuntimeConfigKey } from '../../redesign/helpers/constants';
import { YBBanner, YBBannerVariant } from '../common/descriptors';

const CONFIG_DATA_TYPE_TO_TOOLTIP_MESSAGE = {
  Bytes: 'BytesTooltipMessage',
  Duration: 'DurationTooltipMessage',
  Integer: 'IntegerTooltipMessage',
  Long: 'LongTooltipMessage',
  String: 'StringTooltipMessage',
  'String List': 'StringListTooltipMessage'
};

const STRING_CONVERSION_NEEDED = ['Boolean', 'Tags List'];

interface EditConfigData {
  configData: RunTimeConfigData;
  onHide: () => void;
  fetchUiTagFilter: () => void;
  setRuntimeConfig: (key: string, value: string, scope?: string) => void;
  scope?: string;
  universeUUID?: string;
  providerUUID?: string;
  customerUUID?: string;
}

interface EditConfigDataValues {
  config_value: boolean | string;
  config_key: string;
}

export const EditConfig: FC<EditConfigData> = ({
  configData,
  onHide,
  setRuntimeConfig,
  fetchUiTagFilter,
  scope,
  universeUUID,
  providerUUID,
  customerUUID
}) => {
  const { t } = useTranslation();
  const formik = useRef({} as FormikProps<EditConfigDataValues>);

  const initialValues = {
    config_value:
      configData.type === 'Boolean' ? configData.configValue === 'true' : configData.configValue
  };

  const handleSubmit = async (
    values: EditConfigDataValues,
    { setSubmitting }: { setSubmitting: any; setFieldError: any }
  ) => {
    setSubmitting(false);
    if (isEmptyObject(values)) {
      toast.warn(t('admin.advanced.globalConfig.EditConfigWarningMessage'));
    } else {
      let configScope: string = DEFAULT_RUNTIME_GLOBAL_SCOPE;
      if (scope === RunTimeConfigScope.UNIVERSE) {
        configScope = universeUUID!;
      } else if (scope === RunTimeConfigScope.PROVIDER) {
        configScope = providerUUID!;
      } else if (scope === RunTimeConfigScope.CUSTOMER) {
        configScope = customerUUID!;
      }

      const configValue = STRING_CONVERSION_NEEDED.includes(configData.type)
        ? values.config_value.toString()
        : (values.config_value as string);
      await setRuntimeConfig(configData.configKey, configValue, configScope);
      // Refetch the conf tags to filter runtime config flags accordingly
      if (configData.configKey === RuntimeConfigKey.UI_TAG_FILTER) {
        fetchUiTagFilter();
      }
    }
    onHide();
  };

  return (
    <YBModalForm
      size="large"
      title={t('admin.advanced.globalConfig.ModelEditConfigTitle')}
      visible={true}
      onHide={onHide}
      onFormSubmit={handleSubmit}
      initialValues={initialValues}
      submitLabel="Save"
      showCancelButton
      render={(formikProps: FormikProps<any>) => {
        // workaround for outdated version of Formik to access form methods outside of <Formik>
        formik.current = formikProps;
        const configValue = formik.current.values.config_value;
        return (
          <div>
            <Row>
              <Col lg={8}>
                <Field
                  name="config_key"
                  label={t('admin.advanced.globalConfig.ModalKeyField')}
                  component={YBFormInput}
                  defaultValue={configData.configKey}
                  disabled={true}
                />
              </Col>
              {configData.type === 'Boolean' ? (
                <Col lg={8}>
                  <Field name="config_value">
                    {({ field }: FieldProps) => (
                      <YBToggle
                        name="config_value"
                        label={t('admin.advanced.globalConfig.ModalKeyValue')}
                        input={{
                          value: field.value,
                          onChange: field.onChange
                        }}
                        defaultChecked={configData.configValue === 'true'}
                      />
                    )}
                  </Field>
                </Col>
              ) : (
                <Col lg={8}>
                  <Field
                    name="config_value"
                    label={t('admin.advanced.globalConfig.ModalKeyValue')}
                    defaultValue={configData.configValue}
                    component={YBFormInput}
                    disabled={false}
                    infoTitle={`Type: ${configData.type}`}
                    infoContent={t(
                      `admin.advanced.globalConfig.${
                        CONFIG_DATA_TYPE_TO_TOOLTIP_MESSAGE[configData.type]
                      }`
                    )}
                  />
                </Col>
              )}
            </Row>
            {configData.configKey === RuntimeConfigKey.NODE_AGENT_CLIENT_ENABLE &&
              configValue === false && (
                <YBBanner variant={YBBannerVariant.WARNING}>
                  <Typography variant="body2">
                    {t('nodeAgent.runtimeConfig.nodeAgentClientEnabledWarning')}
                  </Typography>
                </YBBanner>
              )}
          </div>
        );
      }}
    />
  );
};
