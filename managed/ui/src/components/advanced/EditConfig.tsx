
import React, { FC } from 'react';
import { Row, Col } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { Field } from 'formik';

import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '../../actions/customers';
import { RunTimeConfigData, RunTimeConfigScope } from '../../redesign/helpers/dtos';
import { YBModalForm } from '../common/forms';
import { YBFormInput } from '../common/forms/fields';
import { isEmptyObject } from '../../utils/ObjectUtils';

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
      toast.success("Saved Config Successfully");
    } else {
      let scopeValue: string = DEFAULT_RUNTIME_GLOBAL_SCOPE;
      if (scope === RunTimeConfigScope.UNIVERSE) {
        scopeValue = universeUUID!;
      } else if (scope === RunTimeConfigScope.PROVIDER) {
        scopeValue = providerUUID!;
      } else if (scope === RunTimeConfigScope.CUSTOMER) {
        scopeValue = customerUUID!;
      }
      await setRuntimeConfig(
        configData.configKey,
        values.config_value,
        scopeValue
      );
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

            <Col lg={8}>
              <Field
                name="config_value"
                label={t('admin.advanced.globalConfig.ModalKeyValue')}
                defaultValue={configData.configValue}
                component={YBFormInput}
                disabled={false}
              />
            </Col>
          </Row>
        );
      }}
    />
  );
}
