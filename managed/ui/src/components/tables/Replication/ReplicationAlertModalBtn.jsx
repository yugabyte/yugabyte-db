import React, { useRef, useState } from 'react';
import { useQuery } from 'react-query';
import * as Yup from 'yup';
import { Field } from 'formik';
import { YBButton, YBFormInput, YBFormToggle } from '../../common/forms/fields';
import { YBModalForm } from '../../common/forms';
import { YBLoadingCircleIcon } from '../../common/indicators';
import {
  createAlertDefinitionGroup,
  getAlertDefinitionGroups,
  getAlertDefinitionTemplates,
  updateAlertDefinitionGroup
} from '../../../actions/universe';

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
  const [alertDefinitionGroupUUID, setAlertDefinitionGroupUUID] = useState(null);
  const [submissionError, setSubmissionError] = useState();
  const definitionGroupFilter = {
      name: ALERT_NAME,
      targetUuid: universeUUID
  }

  const { isFetching } = useQuery(
    ['getAlertDefinitionGroups', definitionGroupFilter],
    () => getAlertDefinitionGroups(definitionGroupFilter),
    {
      enabled: isModalVisible,
      onSuccess: (data) => {
        if(Array.isArray(data) && data.length > 0) {
           const group = data[0];
           setAlertDefinitionGroupUUID(group.uuid);

           // update form value via workaround as initial form value inside <YBModalForm> is set when
           // it rendered for the first time and we don't have an API response at that time yet
           formik.current.setValues({
             enableAlert: group.active,
             lagThreshold: group.thresholds.SEVERE.threshold
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
    const groupTemplates = await getAlertDefinitionTemplates(templateFilter);
    const template = groupTemplates[0]
    template.active = values.enableAlert;
    template.thresholds.SEVERE.threshold = values.lagThreshold;
    template.target = {
      all: false,
      uuids: [universeUUID]
    };

    try {
      if (alertDefinitionGroupUUID) {
        template.uuid = alertDefinitionGroupUUID;
        await updateAlertDefinitionGroup(template);
      } else {
        await createAlertDefinitionGroup(template);
      }

      formikBag.setSubmitting(false);
      toggleModalVisibility();
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
