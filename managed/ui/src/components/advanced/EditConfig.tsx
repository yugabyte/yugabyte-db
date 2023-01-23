import React, { FC } from 'react';
import { Row, Col } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { Field } from 'formik';
import { YBModalForm } from '../common/forms';
import { YBFormInput, YBFormSelect } from '../common/forms/fields';
import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '../../actions/customers';
import { RunTimeConfigData, RunTimeConfigScope } from '../../redesign/helpers/dtos';
import { isEmptyObject } from '../../utils/ObjectUtils';

const EDIT_CONFIG_BOOLEAN_TYPE_OPTIONS = [
  { value: 'true', label: 'True' },
  { value: 'false', label: 'False' }
];

const CONFIG_DATA_TYPE_TO_TOOLTIP_MESSAGE = {
  Bytes: 'BytesTooltipMessage',
  Duration: 'DurationTooltipMessage',
  Integer: 'IntegerTooltipMessage',
  Long: 'LongTooltipMessage',
  String: 'StringTooltipMessage',
  'String List': 'StringListTooltipMessage'
};

interface EditConfigData {
  configData: RunTimeConfigData;
  onHide: () => void;
  setRuntimeConfig: (key: string, value: string, scope?: string) => void;
  scope?: string;
  universeUUID?: string;
  providerUUID?: string;
  customerUUID?: string;
}

export const EditConfig: FC<EditConfigData> = ({
  configData,
  onHide,
  setRuntimeConfig,
  scope,
  universeUUID,
  providerUUID,
  customerUUID
}) => {
  const { t } = useTranslation();
  const handleSubmit = async (
    values: any,
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
      const configValue =
        configData.type === 'Boolean' ? values.config_value.value : values.config_value;
      await setRuntimeConfig(configData.configKey, configValue, configScope);
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
      submitLabel="Save"
      showCancelButton
      render={() => {
        return (
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
                <Field
                  name="config_value"
                  label={t('admin.advanced.globalConfig.ModalKeyValue')}
                  component={YBFormSelect}
                  options={EDIT_CONFIG_BOOLEAN_TYPE_OPTIONS}
                />
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
        );
      }}
    />
  );
};
