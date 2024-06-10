import { FC, useRef, useState } from 'react';
import { Field, FormikActions, FormikProps } from 'formik';
import moment from 'moment';
import * as Yup from 'yup';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { browserHistory } from 'react-router';
import { Alert } from 'react-bootstrap';
import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { AxiosError } from 'axios';

import { YBModalForm } from '../../common/forms';
import { api, QUERY_KEY } from '../../../redesign/helpers/api';
import { YBLoading } from '../../common/indicators';
import { YBCheckBox, YBFormSelect } from '../../common/forms/fields';
import { handleServerError } from '../../../utils/errorHandlingUtils';
import InfoIcon from '../../../redesign/assets/info-message.svg';
import { YBInput, YBTooltip } from '../../../redesign/components';

import './PromoteInstanceModal.scss';

interface PromoteInstanceModalProps {
  visible: boolean;
  onClose(): void;
  configId: string;
  instanceId: string;
}

interface PromoteInstanceFormValues {
  backupFile: { value: string; label: string } | null;
  isForcePromote: boolean;
}

const INITIAL_VALUES: PromoteInstanceFormValues = {
  backupFile: null,
  isForcePromote: false
};

const validationSchema = Yup.object().shape({
  backupFile: Yup.object().nullable().required('Backup file is required')
});

const adaptHaBackupToFormFieldOption = (value: string): PromoteInstanceFormValues['backupFile'] => {
  // backup_21-02-20-00-40.tgz --> 21-02-20-00-40
  const timestamp = value.replace('backup_', '').replace('.tgz', '');
  const label = moment.utc(timestamp, 'YY-MM-DD-HH:mm').local().format('LLL');

  return { value, label };
};

const POST_PROMOTION_REDIRECT_URL = '/login';

const useStyles = makeStyles((theme) => ({
  confirmTextInputBox: {
    fontWeight: 500,
    width: '350px'
  },
  fieldLabel: {
    marginBottom: theme.spacing(1)
  }
}));

export const PromoteInstanceModal: FC<PromoteInstanceModalProps> = ({
  visible,
  onClose,
  configId,
  instanceId
}) => {
  const [confirmationText, setConfirmationText] = useState<string>('');
  const formik = useRef({} as FormikProps<PromoteInstanceFormValues>);
  const theme = useTheme();
  const classes = useStyles();
  const queryClient = useQueryClient();

  const { isLoading, data } = useQuery(
    [QUERY_KEY.getHABackups, configId],
    () => api.getHABackups(configId),
    {
      enabled: visible,
      onSuccess: (data) => {
        // pre-select first backup file from the list
        if (Array.isArray(data) && data.length) {
          formik.current.setFieldValue('backupFile', adaptHaBackupToFormFieldOption(data[0]));
        }
      }
    }
  );
  const promoteHaInstanceMutation = useMutation(
    (formValues: PromoteInstanceFormValues) =>
      api.promoteHAInstance(configId, instanceId, formValues.isForcePromote, {
        backup_file: formValues.backupFile?.value ?? ''
      }),
    {
      onSuccess(_) {
        queryClient.invalidateQueries(QUERY_KEY.getHAConfig);
        browserHistory.push(POST_PROMOTION_REDIRECT_URL);
        onClose();
      },
      onError: (error: Error | AxiosError) => {
        handleServerError(error, { customErrorLabel: 'Failed to promote platform instance' });
      }
    }
  );

  const backupsList = (data ?? []).map(adaptHaBackupToFormFieldOption);

  const closeModal = () => {
    if (!formik.current.isSubmitting) {
      setConfirmationText('');
      onClose();
    }
  };

  const submitForm = async (
    formValues: PromoteInstanceFormValues,
    actions: FormikActions<PromoteInstanceFormValues>
  ) => {
    return promoteHaInstanceMutation.mutate(formValues, {
      onSettled: () => actions.setSubmitting(false)
    });
  };

  const isSubmitDisabled = confirmationText !== 'PROMOTE';

  if (visible) {
    return (
      <YBModalForm
        visible
        initialValues={INITIAL_VALUES}
        validationSchema={validationSchema}
        submitLabel="Continue"
        cancelLabel="Cancel"
        showCancelButton
        title="Make Active"
        onHide={closeModal}
        onFormSubmit={submitForm}
        isSubmitDisabled={isSubmitDisabled}
        footerAccessory={
          <Box display="flex" gridGap={theme.spacing(1)}>
            <Field
              name="isForcePromote"
              dataTestId="PromoteInstanceModal-IsForcePromoteCheckbox"
              component={YBCheckBox}
              label="Force promotion"
            />
            {/* This tooltip needs to be have a z-index greater than the z-index on the modal (3100)*/}
            <YBTooltip
              title={
                <Typography variant="body2">
                  When the HA standby instance is unable to reach the HA primary, promotion will not
                  be allowed by YBA unless this force promote option is on.
                </Typography>
              }
              PopperProps={{ style: { zIndex: 4000, pointerEvents: 'auto' } }}
            >
              <img src={InfoIcon} />
            </YBTooltip>
          </Box>
        }
        render={(formikProps: FormikProps<PromoteInstanceFormValues>) => {
          // workaround for outdated version of Formik to access form methods outside of <Formik>
          formik.current = formikProps;

          return (
            <div data-testid="ha-make-active-modal">
              {isLoading ? (
                <YBLoading />
              ) : (
                <div className="ha-promote-instance-modal">
                  <Alert bsStyle="warning">
                    Note: promotion will replace all existing data on this platform instance with
                    the data from the selected backup. After promotion succeeds you will need to
                    re-sign in with the credentials of the previously active platform instance.
                  </Alert>
                  <Typography variant="body2" className={classes.fieldLabel}>
                    <Field
                      name="backupFile"
                      component={YBFormSelect}
                      options={backupsList}
                      label="Select the backup to restore from"
                      isSearchable
                    />
                    Please type PROMOTE to confirm.
                  </Typography>
                  <YBInput
                    className={classes.confirmTextInputBox}
                    inputProps={{ 'data-testid': 'PromoteInstanceModal-ConfirmTextInputField' }}
                    placeholder="PROMOTE"
                    value={confirmationText}
                    onChange={(event) => setConfirmationText(event.target.value)}
                  />
                </div>
              )}
            </div>
          );
        }}
      />
    );
  } else {
    return null;
  }
};
