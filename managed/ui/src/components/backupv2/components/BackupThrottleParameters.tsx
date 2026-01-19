/*
 * Created on Thu Sep 01 2022
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import { useDispatch } from 'react-redux';
import { Field, FormikProps } from 'formik';
import { Col, Row } from 'react-bootstrap';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { get } from 'lodash';
import {
  fetchThrottleParameters,
  resetThrottleParameterToDefaults,
  setThrottleParameters
} from '../common/BackupAPI';
import { YBModalForm } from '../../common/forms';
import { YBButton, YBControlledNumericInput } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import { ThrottleParameters, ThrottleParamsVal } from '../common/IBackup';
import { useRefetchTasks } from '@app/redesign/features/tasks/TaskUtils';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '@app/actions/universe';
import * as Yup from 'yup';

import { toast } from 'react-toastify';
import { createErrorMessage } from '../../../utils/ObjectUtils';
import { YBConfirmModal } from '../../modals';
import { YBTag, YBTag_Types } from '../../common/YBTag';
import './BackupThrottleParameters.scss';

interface BackupThrottleParametersProps {
  visible: boolean;
  onHide: () => void;
  currentUniverseUUID: string;
}

export const BackupThrottleParameters: FC<BackupThrottleParametersProps> = ({
  visible,
  onHide,
  currentUniverseUUID
}) => {
  const [showRestoreDefaultModal, setShowRestoreDefaultModal] = useState(false);

  const queryClient = useQueryClient();
  const dispatch = useDispatch();
  const refetchTasks = useRefetchTasks();

  const refreshCurrentUniverse = () => {
    dispatch(fetchUniverseInfo(currentUniverseUUID) as any).then((response: any) => {
      dispatch(fetchUniverseInfoResponse(response.payload));
      refetchTasks();
    });
  };

  const { data: throttleParameters, isLoading } = useQuery(
    ['throttle_parameters', currentUniverseUUID],
    () => fetchThrottleParameters(currentUniverseUUID),
    {
      enabled: visible,
      onError: () => {
        toast.error('Unable to fetch throttle parameter configurations!.');
        onHide();
      }
    }
  );

  const configureThrottleParameters = useMutation(
    (values: ThrottleParameters['throttleParamsMap']) =>
      setThrottleParameters(currentUniverseUUID, values),
    {
      onSuccess: () => {
        toast.success(`Parameters are being updated!.`);
        queryClient.invalidateQueries(['throttle_parameters', currentUniverseUUID]);
        refreshCurrentUniverse();
        onHide();
      },
      onError: (err: any) => {
        toast.error(createErrorMessage(err));
      }
    }
  );

  const resetParameters = useMutation(() => resetThrottleParameterToDefaults(currentUniverseUUID), {
    onSuccess: () => {
      toast.success(`Parameters are being reset to their default values.`);
      queryClient.invalidateQueries(['throttle_parameters', currentUniverseUUID]);
      refreshCurrentUniverse();
      onHide();
    },
    onError: (err: any) => {
      toast.error(createErrorMessage(err));
    }
  });

  const convertBytesToMB = (bytes: number) => {
    return (bytes / (1024 * 1024));
  };

  const convertMBToBytes = (mb: number) => {
    return mb * 1024 * 1024;
  };

  if (!visible) {
    return null;
  }

  if (isLoading) {
    return <YBLoading />;
  }

  const initialValues: ThrottleParameters['throttleParamsMap'] = {
    ...throttleParameters!.data.throttleParamsMap,
    disk_read_bytes_per_sec: {
      ...throttleParameters!.data.throttleParamsMap.disk_read_bytes_per_sec,
      currentValue: convertBytesToMB(throttleParameters!.data.throttleParamsMap.disk_read_bytes_per_sec.currentValue)
    },
    disk_write_bytes_per_sec: {
      ...throttleParameters!.data.throttleParamsMap.disk_write_bytes_per_sec,
      currentValue: convertBytesToMB(throttleParameters!.data.throttleParamsMap.disk_write_bytes_per_sec.currentValue)
    }
  };

  const getPresetValues = (
    path: keyof ThrottleParameters['throttleParamsMap'],
    key: 'min' | 'max'
  ) => {
    return get(initialValues, `${path}.presetValues.${key}Value`);
  };

  const validationSchema = Yup.object().shape({
    max_concurrent_uploads: Yup.object().shape({
      currentValue: Yup.number()
        .required('Required')
        .typeError('Required')
        .min(
          getPresetValues('max_concurrent_uploads', 'min'),
          `Min limit is ${getPresetValues('max_concurrent_uploads', 'min')}`
        )
        .max(
          getPresetValues('max_concurrent_uploads', 'max'),
          `Max limit is ${getPresetValues('max_concurrent_uploads', 'max')}`
        )
    }),

    per_upload_num_objects: Yup.object().shape({
      currentValue: Yup.number()
        .required('Required')
        .typeError('Required')
        .min(
          getPresetValues('per_upload_num_objects', 'min'),
          `Min Limit is ${getPresetValues('per_upload_num_objects', 'min')}`
        )
        .max(
          getPresetValues('per_upload_num_objects', 'max'),
          `Max limit is ${getPresetValues('per_upload_num_objects', 'max')}`
        )
    }),
    max_concurrent_downloads: Yup.object().shape({
      currentValue: Yup.number()
        .required('Required')
        .typeError('Required')
        .min(
          getPresetValues('max_concurrent_downloads', 'min'),
          `Min limit is ${getPresetValues('max_concurrent_downloads', 'min')}`
        )
        .max(
          getPresetValues('max_concurrent_downloads', 'max'),
          `Max limit is ${getPresetValues('max_concurrent_downloads', 'max')}`
        )
    }),
    per_download_num_objects: Yup.object().shape({
      currentValue: Yup.number()
        .required('Required')
        .typeError('Required')
        .min(
          getPresetValues('per_download_num_objects', 'min'),
          `Min Limit is ${getPresetValues('per_download_num_objects', 'min')}`
        )
        .max(
          getPresetValues('per_download_num_objects', 'max'),
          `Max limit is ${getPresetValues('per_download_num_objects', 'max')}`
        )
    }),
    disk_read_bytes_per_sec: Yup.object().shape({
      currentValue: Yup.number()
        .required('Required')
        .typeError('Required')
        .test(
          'is-greater-than-zero',
          `Must be 0 or greater than ${convertBytesToMB(getPresetValues('disk_read_bytes_per_sec', 'min'))} MB`,
          (value) => value !== null && value !== undefined && (value === 0 || value >= convertBytesToMB(getPresetValues('disk_read_bytes_per_sec', 'min')))
        )
    }),
    disk_write_bytes_per_sec: Yup.object().shape({
      currentValue: Yup.number()
        .required('Required')
        .typeError('Required')
        .test(
          'is-greater-than-zero',
          `Must be 0 or greater than ${convertBytesToMB(getPresetValues('disk_write_bytes_per_sec', 'min'))} MB`,
          (value) => value !== null && value !== undefined && (value === 0 || value >= convertBytesToMB(getPresetValues('disk_write_bytes_per_sec', 'min')))
        )
    })
  });

  return (
    <>
      <YBModalForm
        visible={visible}
        onHide={() => {
          onHide();
        }}
        title="Configure Resource Throttling"
        initialValues={initialValues}
        validationSchema={validationSchema}
        footerAccessory={
          <YBButton
            btnText="Reset to defaults"
            btnClass="btn"
            onClick={(e: any) => {
              e.preventDefault();
              setShowRestoreDefaultModal(true);
            }}
          />
        }
        showCancelButton
        dialogClassName="throttle-parameters-modal"
        submitLabel="Save"
        onFormSubmit={(
          values: ThrottleParameters['throttleParamsMap'],
          formik: FormikProps<ThrottleParameters>
        ) => {
          const { setSubmitting } = formik;
          setSubmitting(false);
          const payload = {
            ...values,
            disk_read_bytes_per_sec: {
              ...values.disk_read_bytes_per_sec,
              currentValue: convertMBToBytes(values.disk_read_bytes_per_sec.currentValue)
            },
            disk_write_bytes_per_sec: {
              ...values.disk_write_bytes_per_sec,
              currentValue: convertMBToBytes(values.disk_write_bytes_per_sec.currentValue)
            }
          };

          configureThrottleParameters.mutateAsync(payload);
        }}
        render={(formikProps: FormikProps<ThrottleParameters['throttleParamsMap']>) => {
          const { values, setFieldValue, errors } = formikProps;

          return (
            <>
              <Row>
                <Col lg={12} className="no-padding infos">
                  <div>
                    Manage the speed of Backup and Restore operations by configuring resource
                    throttling.
                  </div>
                  <div>
                    For <b>faster</b> backups and restores, enter higher values.
                    <YBTag type={YBTag_Types.YB_GRAY}>
                      Max {getPresetValues('per_download_num_objects', 'max')}
                    </YBTag>
                  </div>
                  <div>
                    For <b>lower impact</b> on database performance, enter lower values.
                    <YBTag type={YBTag_Types.YB_GRAY}>
                      Min {getPresetValues('per_download_num_objects', 'min')}
                    </YBTag>
                  </div>
                  <div>
                    Use appropriate <b>disk throttling</b> values to throttle disk usage. 0 means use maximum available.
                    <YBTag type={YBTag_Types.YB_GRAY}>
                      Min {convertBytesToMB(getPresetValues('disk_read_bytes_per_sec', 'min')).toFixed(0)} MB/s
                    </YBTag>
                  </div>
                </Col>
                <Col lg={12} className="fields">
                  <div className="section">Backups</div>
                  <Row>
                    <Col lg={12} className="no-padding">
                      Number of parallel uploads per node{' '}
                      <span className="text-secondary">
                        - Default {initialValues.max_concurrent_uploads.presetValues.defaultValue}
                      </span>
                    </Col>
                    <Col lg={2} className="no-padding">
                      <Field
                        name="max_concurrent_uploads.currentValue"
                        component={YBControlledNumericInput}
                        val={values.max_concurrent_uploads.currentValue}
                        onInputChanged={(val: number) =>
                          setFieldValue('max_concurrent_uploads.currentValue', val)
                        }
                      />
                      {errors.max_concurrent_uploads?.currentValue && (
                        <span className="err-msg">
                          {errors.max_concurrent_uploads.currentValue}
                        </span>
                      )}
                    </Col>
                  </Row>
                  <Row>
                    <Col lg={12} className="no-padding">
                      Number of buffers per upload per node{' '}
                      <span className="text-secondary">
                        - Default {initialValues.per_upload_num_objects.presetValues.defaultValue}
                      </span>
                    </Col>
                    <Col lg={2} className="no-padding">
                      <Field
                        name="per_upload_num_objects.currentValue"
                        component={YBControlledNumericInput}
                        val={values.per_upload_num_objects.currentValue}
                        onInputChanged={(val: number) =>
                          setFieldValue('per_upload_num_objects.currentValue', val)
                        }
                      />
                      {errors.per_upload_num_objects?.currentValue && (
                        <span className="err-msg">
                          {errors.per_upload_num_objects.currentValue}
                        </span>
                      )}
                    </Col>
                  </Row>
                  <Row>
                    <Col lg={12} className="no-padding">
                      Disk read bytes per second to throttle disk usage during backups
                      <span className="text-secondary">
                        - Default {initialValues.disk_read_bytes_per_sec.presetValues.defaultValue}
                      </span>
                    </Col>
                    <Col lg={5} className="no-padding">
                      <Field
                        name="disk_read_bytes_per_sec.currentValue"
                        component={YBControlledNumericInput}
                        val={values.disk_read_bytes_per_sec.currentValue}
                        onInputChanged={(val: number) =>
                          setFieldValue('disk_read_bytes_per_sec.currentValue', val)
                        }
                      />
                      {errors.disk_read_bytes_per_sec?.currentValue && (
                        <span className="err-msg">
                          {errors.disk_read_bytes_per_sec.currentValue}
                        </span>
                      )}
                    </Col>
                  </Row>
                </Col>
              </Row>
              <Row>
                <Col lg={12} className="fields">
                  <div className="section">Restores</div>
                  <Row>
                    <Col lg={12} className="no-padding">
                      Number of parallel downloads per node{' '}
                      <span className="text-secondary">
                        - Default {initialValues.max_concurrent_downloads.presetValues.defaultValue}
                      </span>
                    </Col>
                    <Col lg={2} className="no-padding">
                      <Field
                        name="max_concurrent_downloads.currentValue"
                        component={YBControlledNumericInput}
                        val={values.max_concurrent_downloads.currentValue}
                        onInputChanged={(val: number) =>
                          setFieldValue('max_concurrent_downloads.currentValue', val)
                        }
                      />
                      {errors.max_concurrent_downloads?.currentValue && (
                        <span className="err-msg">
                          {errors.max_concurrent_downloads.currentValue}
                        </span>
                      )}
                    </Col>
                  </Row>
                  <Row>
                    <Col lg={12} className="no-padding">
                      Number of buffers per download per node{' '}
                      <span className="text-secondary">
                        - Default {initialValues.per_download_num_objects.presetValues.defaultValue}
                      </span>
                    </Col>
                    <Col lg={2} className="no-padding">
                      <Field
                        name="per_download_num_objects.currentValue"
                        component={YBControlledNumericInput}
                        val={values.per_download_num_objects.currentValue}
                        onInputChanged={(val: number) => {
                          setFieldValue('per_download_num_objects.currentValue', val);
                        }}
                      />
                      {errors.per_download_num_objects?.currentValue && (
                        <span className="err-msg">
                          {errors.per_download_num_objects.currentValue}
                        </span>
                      )}
                    </Col>
                  </Row>
                  <Row>
                    <Col lg={12} className="no-padding">
                      Disk write bytes per second to throttle disk usage during restore
                      <span className="text-secondary">
                        - Default {initialValues.disk_write_bytes_per_sec.presetValues.defaultValue}
                      </span>
                    </Col>
                    <Col lg={5} className="no-padding">
                      <Field
                        name="disk_write_bytes_per_sec.currentValue"
                        component={YBControlledNumericInput}
                        val={values.disk_write_bytes_per_sec.currentValue}
                        onInputChanged={(val: number) => {
                          setFieldValue('disk_write_bytes_per_sec.currentValue', val);
                        }}
                      />
                      {errors.disk_write_bytes_per_sec?.currentValue && (
                        <span className="err-msg">
                          {errors.disk_write_bytes_per_sec.currentValue}
                        </span>
                      )}
                    </Col>
                  </Row>
                </Col>
              </Row>
            </>
          );
        }}
      />
      <YBConfirmModal
        name="throttle-parameters-config"
        title="Confirm Reset to defaults"
        visibleModal={showRestoreDefaultModal}
        currentModal={true}
        onConfirm={() => resetParameters.mutateAsync()}
        hideConfirmModal={() => setShowRestoreDefaultModal(false)}
      >
        <div>Are you sure you want to restore the configurations to these default values?</div>
        <br />
        <h5>Backup</h5>
        <div>
          Number of parallel uploads (per node) ={' '}
          <b>{initialValues.max_concurrent_uploads.presetValues.defaultValue}</b>
        </div>
        <div>
          Number of buffers per upload (per node) ={' '}
          <b>{initialValues.per_upload_num_objects.presetValues.defaultValue}</b>
        </div>
        <div>
          Disk read bytes per second to throttle disk usage during backups ={' '}
          <b>{initialValues.disk_read_bytes_per_sec.presetValues.defaultValue}</b>
        </div>
        <br />
        <h5>Restore</h5>
        <div>
          Number of parallel downloads (per node) ={' '}
          <b>{initialValues.max_concurrent_downloads.presetValues.defaultValue}</b>
        </div>
        <div>
          Number of buffers per download (per node) ={' '}
          <b>{initialValues.per_download_num_objects.presetValues.defaultValue}</b>
        </div>
        <div>
          Disk write bytes per second to throttle disk usage during restores ={' '}
          <b>{initialValues.disk_write_bytes_per_sec.presetValues.defaultValue}</b>
        </div>
      </YBConfirmModal>
    </>
  );
};
