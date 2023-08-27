import { useRef, useState } from 'react';
import { useQuery, useQueryClient } from 'react-query';
import * as Yup from 'yup';
import { Field } from 'formik';
import { YBButton, YBFormInput, YBFormToggle } from '../../common/forms/fields';
import { YBModalForm } from '../../common/forms';
import { YBLoadingCircleIcon } from '../../common/indicators';
import {
  createAlertConfiguration,
  getAlertConfigurations,
  getAlertTemplates,
  updateAlertConfiguration
} from '../../../actions/universe';

// DEPRECATED - Replaced with ConfigureMaxLagTimeModal.tsx
// TODO: Remove this.

const ALERT_NAME = 'Replication Lag';
const DEFAULT_THRESHOLD = 180000;

const DEFAULT_FORM_VALUE = {
  enableAlert: false,
  lagThreshold: DEFAULT_THRESHOLD
};

const validationSchema = Yup.object().shape({
  enableAlert: Yup.boolean(),
  lagThreshold: Yup.number().when('enableAlert', {
    is: true,
    then: Yup.number().typeError('Must be a number').required('Required Field'),
    otherwise: Yup.mixed()
  })
});

export const ReplicationAlertModalBtn = ({ universeUUID, disabled }) => {
  const formik = useRef();
  const [isModalVisible, setModalVisible] = useState(false);
  const [alertConfigurationUUID, setAlertConfigurationUUID] = useState(null);
  const [submissionError, setSubmissionError] = useState();
  const configurationFilter = {
    name: ALERT_NAME,
    targetUuid: universeUUID
  };
  const queryClient = useQueryClient();
  const { isFetching } = useQuery(
    ['getAlertConfigurations', configurationFilter],
    () => getAlertConfigurations(configurationFilter),
    {
      enabled: isModalVisible,
      onSuccess: (data) => {
        if(Array.isArray(data) && data.length > 0) {
          const configuration = data[0];
          setAlertConfigurationUUID(configuration.uuid);

          // update form value via workaround as initial form value inside <YBModalForm> is set when
          // it rendered for the first time and we don't have an API response at that time yet
          formik.current.setValues({
            enableAlert: configuration.active,
            lagThreshold: configuration.thresholds.SEVERE.threshold
          });

        }
      }
    }
  );

  const toggleModalVisibility = () => {
    if (isModalVisible) {
      // don't close modal when form is submitting
      if (!formik.current.isSubmitting) {
        setModalVisible(false);
        setSubmissionError(null);
      }
    } else {
      setModalVisible(true);
    }
  };

  const submit = async (values, formikBag) => {
    const templateFilter = {
      name: ALERT_NAME
    };
    const alertTemplates = await getAlertTemplates(templateFilter);
    const template = alertTemplates[0];
    template.active = values.enableAlert;
    template.thresholds.SEVERE.threshold = values.lagThreshold;
    template.target = {
      all: false,
      uuids: [universeUUID]
    };

    try {
      if (alertConfigurationUUID) {
        template.uuid = alertConfigurationUUID;
        await updateAlertConfiguration(template);
      } else {
        await createAlertConfiguration(template);
      }

      formikBag.setSubmitting(false);
      toggleModalVisibility();
      queryClient.invalidateQueries(['alert', 'configurations', configurationFilter]);
    } catch (error) {
      setSubmissionError(error.message);
      formikBag.setSubmitting(false);
    }
  };

  return (
    <>
      <YBButton
        disabled={disabled}
        btnClass="btn btn-orange"
        btnText="Configure Alert"
        onClick={toggleModalVisibility}
      />

      <YBModalForm
        initialValues={DEFAULT_FORM_VALUE}
        validationSchema={validationSchema}
        onFormSubmit={submit}
        onHide={toggleModalVisibility}
        title="Configure Replication Alert"
        visible={isModalVisible}
        error={submissionError}
        showCancelButton
        submitLabel="Save"
        render={(formikProps) => {
          // workaround to access form methods outside of <Formik> render method
          formik.current = formikProps;

          return isFetching ? (
            <YBLoadingCircleIcon size="medium" />
          ) : (
            <>
              <Field
                name="enableAlert"
                component={YBFormToggle}
                label="Enable Alert"
                subLabel="Notify if average lag is above threshold"
              />
              <br />
              <Field
                name="lagThreshold"
                component={YBFormInput}
                type="number"
                label="Threshold (ms)"
                disabled={!formikProps.values.enableAlert}
              />
            </>
          );
        }}
      />
    </>
  );
};
