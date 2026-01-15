/*
 * Created on Mon Aug 29 2022
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { ChangeEvent, FC, useRef, useState } from 'react';
import { useMutation, useQuery } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { toast } from 'react-toastify';
import { Alert, Col, Row } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { Box, Typography } from '@material-ui/core';
import { isEmpty } from 'lodash';
import { Field, FieldArray, FormikProps } from 'formik';
import { YBModalForm } from '../../common/forms';
import { YBLoading } from '../../common/indicators';
import { YBButton as YBRedesignedButton } from '../../../redesign/components';
import { YBButton, YBCheckBox, YBModal } from '../../common/forms/fields';
import { YBCheckbox, YBInput, YBLabel } from '../../../redesign/components';
import { validateHelmYAML, fetchNodeDetails } from '../../../actions/universe';
import {
  createErrorMessage,
  isEmptyObject,
  isNonEmptyString,
  isEmptyArray,
  isDefinedNotNull
} from '../../../utils/ObjectUtils';
import { getPrimaryCluster } from '../../../utils/UniverseUtils';
import { useIsTaskNewUIEnabled } from '../../../redesign/features/tasks/TaskUtils';
import Close from '../../universes/images/close.svg?img';
import './HelmOverrides.scss';

interface HelmOverridesType {
  universeOverrides: string;
  azOverrides: string[];
  rollingUpgrade: boolean;
  timeDelay: number;
}

interface HelmOverridesUniversePage {
  getConfiguretaskParams: () => Record<string, any>;
  setHelmOverridesData: (helmYAML: any) => void;
}

// Helm override component to be shown on universe page
// eslint-disable-next-line no-redeclare
export const HelmOverridesUniversePage: FC<HelmOverridesUniversePage> = ({
  getConfiguretaskParams,
  setHelmOverridesData
}) => {
  const [showOverrideModal, setShowOverrideModal] = useState(false);
  //fetch existing values from form(state)
  const formValues = useSelector((state: any) => state?.form?.UniverseForm?.values?.primary);

  const editValues: { universeOverrides?: string; azOverrides?: string[] } = {};

  if (formValues) {
    editValues['universeOverrides'] = formValues.universeOverrides;
    if (formValues.azOverrides) {
      editValues['azOverrides'] = Object.keys(formValues.azOverrides).map(
        (k) => k + `:\n${formValues.azOverrides[k]}`
      );
    }
  }

  const formAlreadyFilled =
    !isEmptyObject(editValues) &&
    (isNonEmptyString(editValues['universeOverrides']) ||
      (isDefinedNotNull(editValues['azOverrides']) && !isEmptyArray(editValues['azOverrides'])));

  return (
    <div className="helm-overrides">
      <YBButton
        btnText={`${formAlreadyFilled ? 'Edit' : 'Add'} Kubernetes Overrides`}
        btnIcon={`fa ${formAlreadyFilled ? 'fa-pencil' : 'fa-plus'}`}
        btnSize="small"
        btnClass="btn btn-orange add-overrides-btn"
        onClick={() => {
          setShowOverrideModal(true);
        }}
      />
      <HelmOverridesModal
        visible={showOverrideModal}
        onHide={() => setShowOverrideModal(false)}
        getConfiguretaskParams={getConfiguretaskParams}
        submitForm={setHelmOverridesData}
        editValues={editValues}
        editMode={false}
        forceUpdate={true}
      />
    </div>
  );
};

interface HelmOverridesModalProps {
  visible: boolean;
  submitLabel?: string;
  onHide: () => void;
  getConfiguretaskParams: () => Record<string, any>;
  submitForm: (helmYAML: any) => void;
  editValues?: Record<string, any>;
  editMode?: boolean;
  forceUpdate?: boolean;
}

interface NodeOverridesModalProps {
  visible: boolean;
  nodeId: string;
  universeId: string;
  onClose: () => void;
}

type validation_errors_initial_schema = {
  overridesErrors: {
    errorString: string;
  }[];
};

const validation_errors_initial_state: validation_errors_initial_schema = {
  overridesErrors: []
};

const UNIVERSE_OVERRIDE_SAMPLE = `master:
  podLabels:
    env: test
tserver:
  podLabels:
    env: test`;
const AZ_OVERRIDE_SAMPLE = `us-west-1a:
  tserver:
    podLabels:
      env: test-us-west-1a`;

/**
 * This component provides an option to override kubernetes config via helm overrides.
 * It will validate the helm YAML and shows error when validation fails
 */

export const HelmOverridesModal: FC<HelmOverridesModalProps> = ({
  visible,
  onHide,
  submitForm,
  getConfiguretaskParams,
  editValues,
  editMode,
  forceUpdate
}) => {
  const { t } = useTranslation();
  const [forceConfirm, setForceConfirm] = useState<boolean>(false);
  const [rollingUpgrade, setRollingUpgrade] = useState<boolean>(true);
  const [timeDelay, setTimeDelay] = useState<number>(180);

  const formik = useRef({} as FormikProps<HelmOverridesType>);

  let initialValues: HelmOverridesType = {
    universeOverrides: '',
    azOverrides: [],
    rollingUpgrade: true,
    timeDelay: 180
  };

  if (editValues) {
    initialValues = {
      ...initialValues,
      ...editValues
    };
  }

  //show helm validation errors from the backend
  const [validationError, setValidationError] = useState<validation_errors_initial_schema>(
    validation_errors_initial_state
  );

  // validate YAML
  const doValidateYAML = useMutation(
    (values: any) => {
      return validateHelmYAML({
        ...values.universeConfigureData
      });
    },
    {
      onSuccess: (resp, reqValues) => {
        const applyHelmOverrides = () => {
          submitForm({
            universeOverrides: reqValues.values.universeOverrides,
            azOverrides: reqValues.values.azOverrides,
            rollingUpgrade: rollingUpgrade,
            timeDelay: timeDelay,
            runOnlyPrechecks: reqValues.values.runOnlyPrechecks
          });
          setValidationError(validation_errors_initial_state);
        };

        if (resp.data.overridesErrors.length > 0) {
          if (forceConfirm) {
            applyHelmOverrides();
          } else {
            setValidationError(resp.data);
          }
        } else {
          applyHelmOverrides();
        }
      },
      onError: (err: any, reqValues) => {
        // sometimes, the backend throws 500 error, if the validation is failed. we don't want to block the user if that happens
        if (err.response.status === 500) {
          onHide();
          submitForm({
            universeOverrides: reqValues.values.universeOverrides,
            azOverrides: reqValues.values.azOverrides,
            rollingUpgrade: rollingUpgrade,
            timeDelay: timeDelay,
            runOnlyPrechecks: reqValues.values.runOnlyPrechecks
          });
        } else {
          toast.error(createErrorMessage(err));
        }
      }
    }
  );

  const handleFormSubmit = async (runOnlyPrechecks = false) => {
    const currentFormik = formik.current;
    const values = currentFormik.values;

    const universeConfigureData = getConfiguretaskParams();
    const universeUUID = universeConfigureData.universeUUID;

    const primaryCluster = getPrimaryCluster(universeConfigureData.clusters);

    if (values.universeOverrides) {
      primaryCluster.userIntent.universeOverrides = values.universeOverrides;
    }

    /* format
    azOverrides = {
      "az-region1": "override yaml",
      "az-region2": "override yaml",
    }
    */

    const azOverrides: { [key: string]: string } = {};
    if (values.azOverrides.length > 0) {
      values.azOverrides.forEach((a: any) => {
        if (a.length === 0) return;
        const regionIndex = a.indexOf('\n');
        const region = a.substring(0, regionIndex).trim().replace(':', '');
        const regionOverride = a.substring(regionIndex + 1);
        if (region && regionOverride) azOverrides[region] = regionOverride;
      });
    }

    primaryCluster.userIntent.azOverrides = azOverrides;

    //no instance tags for k8s
    delete primaryCluster.userIntent.instanceTags;

    doValidateYAML
      .mutateAsync({
        universeConfigureData,
        values: {
          universeOverrides: values.universeOverrides,
          azOverrides,
          runOnlyPrechecks,
          universeUUID
        }
      })
      .then(() => currentFormik.setSubmitting(false))
      .catch(() => currentFormik.setSubmitting(false));
  };

  const onTimeDelayChange = (delay: number) => {
    setTimeDelay(delay);
  };

  const handleRollingUpgrade = (event: ChangeEvent<HTMLInputElement>) => {
    const isChecked = event.target.checked;
    setRollingUpgrade(isChecked);
  };

  const showAlert = validationError?.overridesErrors.length !== 0;

  return (
    <YBModalForm
      title={t('universeActions.helmOverrides.title')}
      showSubmitButton={false}
      size="large"
      visible={visible}
      formName="HelmOverridesForm"
      className="helm-overrides-form"
      initialValues={initialValues}
      onHide={onHide}
      footerAccessory={
        <div
          style={{
            display: 'flex',
            flexDirection: 'row'
          }}
        >
          <div>
            {forceUpdate ? (
              <YBCheckBox
                label={
                  editMode
                    ? t('universeActions.helmOverrides.forceUpgrade')
                    : t('universeActions.helmOverrides.forceApply')
                }
                input={{
                  checked: forceConfirm,
                  onChange: () => setForceConfirm(!forceConfirm)
                }}
                className="yb-input-checkbox"
              />
            ) : null}
          </div>
          <div
            style={{
              display: 'flex',
              gap: '8px',
              // TODO: Ideally we need to change the YBModalForm footerAccessory styles as it uses pull-left or pull-right which does not allow us
              // to individually float elements to left or right of our choice
              marginLeft: '410px'
            }}
          >
            <YBRedesignedButton variant="secondary" size="large" data-testid="HelmOverrides-Cancel">
              {t('common.cancel')}
            </YBRedesignedButton>
            <YBRedesignedButton
              variant="secondary"
              size="large"
              onClick={() => {
                handleFormSubmit(true);
              }}
              data-testid="HelmOverrides-Precheck"
            >
              {t('universeActions.runPrecheckOnlyButton')}
            </YBRedesignedButton>
            <YBRedesignedButton
              variant="primary"
              size="large"
              onClick={() => {
                handleFormSubmit(false);
              }}
              data-testid="HelmOverrides-UpgradeButton"
            >
              {t('common.upgrade')}
            </YBRedesignedButton>
          </div>
        </div>
      }
      dialogClassName={visible ? 'modal-fade in' : 'modal-fade'}
      headerClassName="add-flag-header"
      render={(formikProps: FormikProps<HelmOverridesType>) => {
        formik.current = formikProps;
        return (
          <>
            {showAlert && (
              <Alert bsStyle="danger" className="overrides-errors">
                <>
                  {!isEmpty(validationError) && (
                    <>
                      <b>Errors in helm YAML</b>
                      {validationError.overridesErrors.map((e, index) => (
                        <div key={index}>
                          {index + 1}.&nbsp;{e.errorString}
                          <br />
                        </div>
                      ))}
                    </>
                  )}
                </>
              </Alert>
            )}
            <b className="helm-fields">Universe Overrides:</b>
            <Field
              component="textarea"
              name="universeOverrides"
              className="helm-overrides-text"
              placeholder={UNIVERSE_OVERRIDE_SAMPLE}
            />
            <br />
            <br />
            <b className="helm-fields">AZ Overrides:</b>
            <FieldArray
              name="azOverrides"
              render={(arrayHelper) => (
                <>
                  <div className="az-overrides">
                    {formikProps.values.azOverrides.map((_az, index) => {
                      return (
                        <Row key={index}>
                          <Col lg={12} className="az-override-row">
                            <div>
                              <b className="az-override-label">Availability Zone {index + 1}:</b>
                              <Field
                                name={`azOverrides.${index}`}
                                component="textarea"
                                placeholder={AZ_OVERRIDE_SAMPLE}
                              />
                            </div>
                            <img
                              alt="Remove"
                              className="remove-field-icon"
                              src={Close}
                              width="22"
                              onClick={() => arrayHelper.remove(index)}
                            />
                          </Col>
                        </Row>
                      );
                    })}
                  </div>
                  <a
                    href="#!"
                    className="on-prem-add-link add-region-link helm-fields"
                    onClick={(e) => {
                      e.preventDefault();
                      arrayHelper.push('');
                    }}
                  >
                    <i className="fa fa-plus-circle" />
                    {t('universeActions.helmOverrides.addAvailabilityZone')}
                  </a>
                </>
              )}
            />
            <Box mt={2}>
              <YBCheckbox
                label={t('universeActions.helmOverrides.rollingUpgradeLabel')}
                style={{ width: 'fit-content' }}
                checked={rollingUpgrade}
                onChange={handleRollingUpgrade}
                inputProps={{
                  'data-testid': 'HelmOverrides-RollingUpgrade'
                }}
              />
              <Box display={'flex'} flexDirection={'row'} width="100%" alignItems={'center'} ml={1}>
                <YBLabel width="210px">
                  {t('universeActions.helmOverrides.upgradeDelayLabel')}
                </YBLabel>
                <Box width="160px" mr={1}>
                  <YBInput
                    type="number"
                    value={timeDelay}
                    onChange={(event: any) => onTimeDelayChange(event.target.value)}
                    disabled={!rollingUpgrade}
                    fullWidth
                    inputProps={{
                      autoFocus: true,
                      'data-testid': 'HelmOverrides-TimeDelay'
                    }}
                  />
                </Box>
                <Typography variant="body2">{t('common.seconds')}</Typography>
              </Box>
            </Box>
          </>
        );
      }}
    />
  );
};

export const NodeOverridesModal: FC<NodeOverridesModalProps> = ({
  visible,
  onClose,
  nodeId,
  universeId
}) => {
  const { data, isLoading, isError } = useQuery(['NODE_DETAILS', universeId, nodeId], () =>
    fetchNodeDetails(universeId, nodeId)
  );

  const nodeDetails = (data as unknown) as Record<string, any>;

  const renderAppliedOverrides = () => {
    const appliedOverides = nodeDetails?.data?.kubernetesOverrides ?? '';
    if (isLoading) return <YBLoading />;

    if (isError)
      return <Alert bsStyle="danger">Oops! Something went wrong. Please try again.</Alert>;

    if (!appliedOverides) return <Alert bsStyle="warning">No Kubernetes Overrides applied.</Alert>;

    return (
      <textarea disabled={true} className="overrides-textarea">
        {appliedOverides}
      </textarea>
    );
  };

  return (
    <YBModal
      title={
        <>
          Kubernetes Overrides <span>{`(${nodeId})`}</span>
        </>
      }
      visible={visible}
      onHide={onClose}
      showCancelButton={true}
      cancelLabel={'OK'}
    >
      <Row>{renderAppliedOverrides()}</Row>
    </YBModal>
  );
};
