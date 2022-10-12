/*
 * Created on Wed Jun 08 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC } from 'react';
import moment from 'moment';
import clsx from 'clsx';
import { toast } from 'react-toastify';
import { useMutation, useQueryClient } from 'react-query';
import { Field, FormikProps } from 'formik';
import { Col, Row } from 'react-bootstrap';
import { YBModalForm } from '../../common/forms';
import { YBFormSelect, YBNumericInput } from '../../common/forms/fields';
import { DATE_FORMAT } from '../common/BackupUtils';
import { FormatUnixTimeStampTimeToTimezone } from './PointInTimeRecoveryList';
import { restoreSnapShot } from '../common/PitrAPI';
import CautionIcon from '../common/CautionIcon';
import './PointInTimeRecoveryModal.scss';

const reactWidgets = require('react-widgets');
const momentLocalizer = require('react-widgets-moment');
require('react-widgets/dist/css/react-widgets.css');

const { DatePicker, TimePicker } = reactWidgets;
momentLocalizer(moment);

interface PointInTimeRecoveryModalProps {
  visible: boolean;
  onHide: () => void;
  universeUUID: string;
  config: any;
}

enum RECOVERY_MODE {
  'RELATIVE',
  'EXACT'
}

const DURATION_OPTIONS = [
  {
    label: 'Day',
    value: 'DAY',
    seconds: 24 * 60 * 60 * 1000 //milli-secs in a day
  },
  {
    label: 'Hour',
    value: 'HOUR',
    seconds: 60 * 60 * 1000 //milli-secs in an hour
  },
  {
    label: 'Minute',
    value: 'MINUTE',
    seconds: 60 * 1000 //milli-secs in a minute
  }
];

interface Form_Values {
  recovery_time_mode: RECOVERY_MODE;
  recovery_interval: number;
  recovery_duration: Record<string, string | number>;
  customDate?: string;
  customTime?: string;
}

const initialValues: Form_Values = {
  recovery_time_mode: RECOVERY_MODE.RELATIVE,
  recovery_duration: DURATION_OPTIONS[0],
  recovery_interval: 1
};

const TOAST_AUTO_CLOSE_INTERVAL = 3000;

export const PointInTimeRecoveryModal: FC<PointInTimeRecoveryModalProps> = ({
  visible,
  onHide,
  config,
  universeUUID
}) => {
  const queryClient = useQueryClient();

  const createPITR = useMutation((values: any) => restoreSnapShot(universeUUID, values), {
    onSuccess: () => {
      toast.success(`${config.dbName} recovered successfully!`, {
        autoClose: TOAST_AUTO_CLOSE_INTERVAL
      });
      queryClient.invalidateQueries(['scheduled_sanpshots']);
      onHide();
    },
    onError: () => {
      toast.error(`Failed to recover ${config.dbName}.`, { autoClose: TOAST_AUTO_CLOSE_INTERVAL });
      onHide();
    }
  });

  if (!config) return <></>;

  const minTime = config.minRecoverTimeInMillis;
  const maxTime = config.maxRecoverTimeInMillis;

  const getFinalTimeStamp = (values: any) => {
    const {
      recovery_time_mode,
      recovery_duration,
      recovery_interval,
      customDate,
      customTime
    } = values;
    let finalTimeStamp = moment.now();
    if (recovery_time_mode === RECOVERY_MODE.RELATIVE) {
      const secsInSelectedDuration = DURATION_OPTIONS.find(
        (duration: any) => duration.value === recovery_duration.value
      )?.seconds;
      const currentTimeStamp = moment.now();

      if (secsInSelectedDuration) {
        finalTimeStamp = currentTimeStamp - secsInSelectedDuration * Number(recovery_interval);
      }
    }

    if (recovery_time_mode === RECOVERY_MODE.EXACT) {
      const dateTime = new Date(
        customDate.getFullYear(),
        customDate.getMonth(),
        customDate.getDate(),
        customTime.getHours(),
        customTime.getMinutes(),
        customTime.getSeconds()
      );
      finalTimeStamp = moment(dateTime).unix() * 1000;
    }

    return finalTimeStamp;
  };

  const validateForm = (values: any) => {
    const errors = {
      recovery_time_mode: 'Please select a time within your retention period'
    };
    const finalTimeStamp = getFinalTimeStamp(values);
    if (!(finalTimeStamp >= minTime && finalTimeStamp <= maxTime + 60000)) return errors; // delay of 1 min in case if min and max time are the same

    return {};
  };

  const handleSubmit = async (
    values: any,
    { setSubmitting }: { setSubmitting: any; setFieldError: any }
  ) => {
    setSubmitting(false);
    const payload = {
      restoreTimeInMillis: getFinalTimeStamp(values),
      pitrConfigUUID: config.uuid
    };
    createPITR.mutateAsync(payload);
  };

  return (
    <YBModalForm
      title="Recover database-1 to a point in time"
      visible={visible}
      onHide={onHide}
      submitLabel="Recover"
      onFormSubmit={handleSubmit}
      showCancelButton
      dialogClassName="pitr-recovery-modal"
      initialValues={initialValues}
      validate={validateForm}
      render={({ values, setFieldValue, errors }: FormikProps<Form_Values>) => {
        return (
          <>
            <div className="notice">
              You may recover {config.dbName} to any time between{' '}
              <b>
                <FormatUnixTimeStampTimeToTimezone timestamp={minTime} />
              </b>{' '}
              and{' '}
              <b>
                <FormatUnixTimeStampTimeToTimezone timestamp={maxTime} />
              </b>
            </div>
            <Row>
              <Col lg={6} className="no-padding">
                <Field
                  name="recovery_time_mode"
                  checked={values['recovery_time_mode'] === RECOVERY_MODE.RELATIVE}
                  component={() => {
                    return (
                      <div
                        className={clsx('pitr-custom-radio center-align', {
                          active: values['recovery_time_mode'] === RECOVERY_MODE.RELATIVE
                        })}
                      >
                        <input
                          type="radio"
                          checked={values['recovery_time_mode'] === RECOVERY_MODE.RELATIVE}
                          onClick={() => {
                            setFieldValue('recovery_time_mode', RECOVERY_MODE.RELATIVE, true);
                          }}
                        />
                        <div className="relative-time-mode">
                          <Field
                            name="recovery_interval"
                            component={YBNumericInput}
                            input={{
                              onChange: (val: number) =>
                                setFieldValue('recovery_interval', val, true),
                              value: values['recovery_interval']
                            }}
                            minwidth="80px"
                            minVal={1}
                          />
                          <Field
                            className="recovery_duration"
                            component={YBFormSelect}
                            options={DURATION_OPTIONS}
                            width={140}
                            name="recovery_duration"
                          />
                          <span>Ago</span>
                          <div className="break" />
                          {values['recovery_time_mode'] === RECOVERY_MODE.RELATIVE &&
                          errors.recovery_time_mode ? (
                            <div className="pitr-error-text">
                              <CautionIcon />
                              &nbsp;{errors.recovery_time_mode}
                            </div>
                          ) : (
                            <div className="pitr-info-text">
                              Will recover to:{' '}
                              <FormatUnixTimeStampTimeToTimezone
                                timestamp={getFinalTimeStamp({
                                  ...values,
                                  recovery_time_mode: RECOVERY_MODE.RELATIVE
                                })}
                              />
                            </div>
                          )}
                        </div>
                      </div>
                    );
                  }}
                  type="radio"
                />
              </Col>
            </Row>
            <Row>
              <Col lg={6} className="no-padding">
                <Field
                  name="recovery_time_mode"
                  component={() => {
                    return (
                      <div
                        className={clsx('pitr-custom-radio center-align', {
                          active: values['recovery_time_mode'] === RECOVERY_MODE.EXACT
                        })}
                      >
                        <input
                          type="radio"
                          checked={values['recovery_time_mode'] === RECOVERY_MODE.EXACT}
                          onClick={() => {
                            setFieldValue('recovery_time_mode', RECOVERY_MODE.EXACT, true);
                            !values.customDate && setFieldValue('customDate', new Date());
                            !values.customTime && setFieldValue('customTime', new Date());
                          }}
                        />
                        <div className="exact-time-mode">
                          <Row>
                            <Col xs={6} className="no-padding">
                              Date
                              <DatePicker
                                placeholder="Pick a time"
                                formats={DATE_FORMAT}
                                value={values.customDate}
                                max={new Date()}
                                defaultValue={new Date()}
                                onChange={(dt: Date) =>
                                  setFieldValue('customDate' as never, dt, true)
                                }
                              />
                            </Col>

                            <Col xs={6} className="no-padding">
                              Time
                              <TimePicker
                                defaultValue={new Date()}
                                value={values.customTime}
                                onChange={(time: Date) =>
                                  setFieldValue('customTime' as never, time, true)
                                }
                              />
                            </Col>
                          </Row>

                          <Row>
                            <div className="break" />
                            {values['recovery_time_mode'] === RECOVERY_MODE.EXACT &&
                              errors.recovery_time_mode && (
                                <div className="pitr-error-text">
                                  <CautionIcon />
                                  &nbsp;{errors.recovery_time_mode}
                                </div>
                              )}
                          </Row>
                        </div>
                      </div>
                    );
                  }}
                  onChange={() =>
                    setFieldValue('recovery_time_mode' as never, RECOVERY_MODE.EXACT, false)
                  }
                  type="radio"
                />
              </Col>
            </Row>
            <Row></Row>
          </>
        );
      }}
    />
  );
};
