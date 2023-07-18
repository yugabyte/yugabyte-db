import { FC } from 'react';
import { Row, Col } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { Field } from 'formik';

import { RunTimeConfigData, RunTimeConfigScope } from '../../redesign/utils/dtos';
import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '../../actions/customers';
import { YBModalForm } from '../common/forms';
import { YBFormInput } from '../common/forms/fields';

interface ResetConfigData {
  configData: RunTimeConfigData;
  onHide: () => void;
  deleteRunTimeConfig: (key: string, scope?: string) => void;
  scope?: string;
  universeUUID?: string;
  providerUUID?: string;
  customerUUID?: string;
}

export const ResetConfig: FC<ResetConfigData> = ({
  configData,
  onHide,
  deleteRunTimeConfig,
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
    let scopeValue: string = DEFAULT_RUNTIME_GLOBAL_SCOPE;
    if (scope === RunTimeConfigScope.UNIVERSE) {
      scopeValue = universeUUID!;
    } else if (scope === RunTimeConfigScope.PROVIDER) {
      scopeValue = providerUUID!;
    } else if (scope === RunTimeConfigScope.CUSTOMER) {
      scopeValue = customerUUID!;
    }
    await deleteRunTimeConfig(configData.configKey, scopeValue);
    onHide();
  };
  return (
    <YBModalForm
      size="large"
      title={t('admin.advanced.globalConfig.ModelResetConfigTitle')}
      visible={true}
      onHide={onHide}
      onFormSubmit={handleSubmit}
      submitLabel="Reset"
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
          </Row>
        );
      }}
    />
  );
};
