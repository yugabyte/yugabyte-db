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
import { REPLICATION_LAG_ALERT_NAME } from './constants';

import './ConfigureMaxLagTimeModal.scss';

// TODO - Investigation & clean up: https://yugabyte.atlassian.net/browse/PLAT-5766

const validationSchema = Yup.object().shape({
  maxLag: Yup.string().required('maximum lag is required')
});

interface Props {
  onHide: Function;
  visible: boolean;
  currentUniverseUUID: string;
}

interface FormValues {
  maxLag: number;
  enableAlert: boolean;
}

const DEFAULT_THRESHOLD = 180_000;

export function ConfigureMaxLagTimeModal({ onHide, visible, currentUniverseUUID }: Props) {
  const [alertConfigurationUUID, setAlertConfigurationUUID] = useState(null);
  const formik = useRef<any>();
  const queryClient = useQueryClient();

  const initialValues: FormValues = {
    maxLag: DEFAULT_THRESHOLD,
    enableAlert: true
  };

  const configurationFilter = {
    name: REPLICATION_LAG_ALERT_NAME,
    targetUuid: currentUniverseUUID
  };

  const { isLoading, isError } = useQuery(
    ['getAlertConfigurations', configurationFilter],
    () => getAlertConfigurations(configurationFilter),
    {
      enabled: visible,
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
      name: REPLICATION_LAG_ALERT_NAME
    };
    const alertTemplates = await getAlertTemplates(templateFilter);
    const template = alertTemplates[0];
    template.active = values.enableAlert;
    template.thresholds.SEVERE.threshold = values.maxLag;
    template.target = {
      all: false,
      uuids: [currentUniverseUUID]
    };

    try {
      if (alertConfigurationUUID) {
        template.uuid = alertConfigurationUUID;
        await updateAlertConfiguration(template);
      } else {
        await createAlertConfiguration(template);
      }

      formikBag.setSubmitting(false);
      queryClient.invalidateQueries(['alert', 'configurations', configurationFilter]);
      onHide();
    } catch (error) {
      formikBag.setSubmitting(false);
    }
  };

  return (
    <YBModalForm
      visible={visible}
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
