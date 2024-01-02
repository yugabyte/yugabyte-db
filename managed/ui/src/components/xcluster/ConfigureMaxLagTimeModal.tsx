import { useRef, useState } from 'react';
import { Field, FormikFormProps, FormikProps } from 'formik';
import { useQuery, useQueryClient } from 'react-query';
import * as Yup from 'yup';
import { Col, Row } from 'react-bootstrap';

import { YBModalForm } from '../common/forms';
import { YBCheckBox, YBFormInput } from '../common/forms/fields';
import {
  createAlertConfiguration,
  getAlertConfigurations,
  getAlertTemplates,
  updateAlertConfiguration
} from '../../actions/universe';
import { YBErrorIndicator, YBLoading } from '../common/indicators';
import { AlertName } from './constants';
import { alertConfigQueryKey } from '../../redesign/helpers/api';

import './ConfigureMaxLagTimeModal.scss';

// TODO - Investigation & clean up: https://yugabyte.atlassian.net/browse/PLAT-5766

const validationSchema = Yup.object().shape({
  maxLag: Yup.string().required('maximum lag is required')
});

interface Props {
  onHide: Function;
  isVisible: boolean;
  sourceUniverseUuid: string;
}

interface FormValues {
  maxLag: number;
  enableAlert: boolean;
}

const DEFAULT_THRESHOLD = 180_000;

export function ConfigureReplicationLagAlertModal({
  onHide,
  isVisible,
  sourceUniverseUuid
}: Props) {
  const [alertConfigurationUUID, setAlertConfigurationUUID] = useState(null);
  const formik = useRef<any>();
  const queryClient = useQueryClient();

  const initialValues: FormValues = {
    maxLag: DEFAULT_THRESHOLD,
    enableAlert: true
  };

  const alertConfigFilter = {
    name: AlertName.REPLICATION_LAG,
    targetUuid: sourceUniverseUuid
  };
  const { isLoading, isError } = useQuery(
    alertConfigQueryKey.list(alertConfigFilter),
    () => getAlertConfigurations(alertConfigFilter),
    {
      enabled: isVisible,
      onSuccess: (data) => {
        if (Array.isArray(data) && data.length > 0) {
          let lowestThresholdIndex = 0;
          let maxAcceptableLag = data[0].thresholds.SEVERE.threshold;
          data.forEach((alertConfig, index) => {
            if (alertConfig.thresholds.SEVERE.threshold < maxAcceptableLag) {
              lowestThresholdIndex = index;
              maxAcceptableLag = alertConfig.thresholds.SEVERE.threshold;
            }
          });
          const configuration = data[lowestThresholdIndex];
          setAlertConfigurationUUID(configuration.uuid);
          formik.current.setValues({
            enableAlert: configuration.active,
            maxLag: configuration.thresholds.SEVERE.threshold
          });
        }
      }
    }
  );

  const submit = async (values: FormValues, formikBag: FormikProps<FormValues>) => {
    const templateFilter = {
      name: AlertName.REPLICATION_LAG
    };
    const alertTemplates = await getAlertTemplates(templateFilter);
    const template = alertTemplates[0];
    template.active = values.enableAlert;
    template.thresholds.SEVERE.threshold = values.maxLag;
    template.target = {
      all: false,
      uuids: [sourceUniverseUuid]
    };

    try {
      if (alertConfigurationUUID) {
        template.uuid = alertConfigurationUUID;
        await updateAlertConfiguration(template);
      } else {
        await createAlertConfiguration(template);
      }

      formikBag.setSubmitting(false);
      queryClient.invalidateQueries(alertConfigQueryKey.list(alertConfigFilter));
      onHide();
    } catch (error) {
      formikBag.setSubmitting(false);
    }
  };

  return (
    <YBModalForm
      visible={isVisible}
      title="Define Max Acceptable Lag Time"
      validationSchema={validationSchema}
      onFormSubmit={submit}
      initialValues={initialValues}
      submitLabel="Save"
      onHide={onHide}
      showCancelButton
      render={(formikProps: FormikFormProps) => {
        formik.current = formikProps;
        if (isLoading) {
          return <YBLoading />;
        }
        if (isError) {
          return <YBErrorIndicator />;
        }
        return (
          <div className="maxLagForm">
            <Row className="marginTop">
              <Col lg={12}>
                <Field
                  name="maxLag"
                  placeholder="Maximum acceptable lag in milliseconds"
                  label="Define maximum acceptable replication lag time for this universe in milliseconds"
                  component={YBFormInput}
                />
              </Col>
            </Row>
            <Row className="marginTop">
              <Col lg={12}>
                <Field
                  name="enableAlert"
                  label={<span className="checkbox-label">Enable notification</span>}
                  component={YBCheckBox}
                  checkState={(formikProps as any).values['enableAlert']}
                />
                <br />
                <span className="alert-subtext">
                  {"We'll email you if a replication lag exceeds the defined value above"}
                </span>
              </Col>
            </Row>
          </div>
        );
      }}
    />
  );
}
