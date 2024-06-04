/*
 * Created on Tue Jun 07 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import * as Yup from 'yup';
import { Field, FormikProps } from 'formik';
import { toast } from 'react-toastify';
import { Col, Row } from 'react-bootstrap';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { YBModalForm } from '../../common/forms';
import { YBFormSelect, YBNumericInput } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import { BACKUP_API_TYPES } from '../common/IBackup';
import { createPITRConfig, getNameSpaces } from '../common/PitrAPI';
import { AllowedTasks, TableTypeLabel } from '../../../redesign/helpers/dtos';
import { isActionFrozen } from '../../../redesign/helpers/utils';
import { UNIVERSE_TASKS } from '../../../redesign/helpers/constants';

import './PointInTimeRecoveryEnableModal.scss';

interface PointInTimeRecoveryEnableModalProps {
  universeUUID: string;
  visible: boolean;
  allowedTasks: AllowedTasks;
  onHide: () => void;
}

const PITR_SUPPORTED_APIS = [
  TableTypeLabel[BACKUP_API_TYPES.YSQL],
  TableTypeLabel[BACKUP_API_TYPES.YCQL]
];

interface Form_Values {
  api_type: Record<string, string>;
  database: string | null;
  retention_interval: number;
}

const initialValues: Form_Values = {
  api_type: { value: BACKUP_API_TYPES.YSQL, label: TableTypeLabel[BACKUP_API_TYPES.YSQL] },
  database: null,
  retention_interval: 7
};

const REFETCH_CONFIGS_INTERVAL = 5000; //ms

export const PointInTimeRecoveryEnableModal: FC<PointInTimeRecoveryEnableModalProps> = ({
  universeUUID,
  visible,
  allowedTasks,
  onHide
}) => {
  const queryClient = useQueryClient();

  const { data: nameSpaces, isLoading } = useQuery(
    [universeUUID, 'namespaces'],
    () => getNameSpaces(universeUUID),
    {
      enabled: visible
    }
  );

  const createPITR = useMutation(
    (values: any) =>
      createPITRConfig(universeUUID, values.tableType, values.keyspaceName, values.payload),
    {
      onSuccess: (resp, variables) => {
        toast.success(
          <span>
            Point-in-time recovery is being enabled for {variables.keyspaceName}. Click &nbsp;
            <a href={`/tasks/${resp.data.taskUUID}`} target="_blank" rel="noopener noreferrer">
              here
            </a>
            &nbsp; for task details.
          </span>
        );
        //refetch after 5 secs
        setTimeout(() => {
          queryClient.invalidateQueries(['scheduled_sanpshots']);
        }, REFETCH_CONFIGS_INTERVAL);
        onHide();
      },
      onError: (err: any) => {
        toast.error(err?.response?.data?.error ?? 'An Error occurred');
        onHide();
      }
    }
  );

  const handleSubmit = async (
    values: any,
    { setSubmitting }: { setSubmitting: any; setFieldError: any }
  ) => {
    setSubmitting(false);
    const tableType = values.api_type.label;
    const keyspaceName = values.database.value;
    const payload = {
      retentionPeriodInSeconds: Number(values.retention_interval) * 24 * 60 * 60
    };
    createPITR.mutateAsync({ tableType, keyspaceName, payload });
  };

  const validationSchema = Yup.object().shape({
    database: Yup.object().nullable().required('Select a database to proceed')
  });

  if (!visible) return null;

  if (isLoading) return <YBLoading />;

  const isCreateActionFrozen = isActionFrozen(allowedTasks, UNIVERSE_TASKS.ENABLE_PITR);

  return (
    <YBModalForm
      title="Enable Point-in-time Recovery"
      visible={visible}
      onHide={onHide}
      isButtonDisabled={isCreateActionFrozen}
      submitLabel="Enable"
      onFormSubmit={handleSubmit}
      showCancelButton
      dialogClassName="pitr-enable-modal"
      submitTestId="EnablePitrSubmitBtn"
      cancelTestId="EnablePitrCancelBtn"
      initialValues={initialValues}
      validationSchema={validationSchema}
      render={({ values, setFieldValue, errors }: FormikProps<Form_Values>) => {
        const nameSpacesByAPI = nameSpaces?.filter(
          (t: any) => t.tableType === values['api_type'].value
        );
        const nameSpacesList = nameSpacesByAPI.map((nameSpace: any) => ({
          label: nameSpace.name,
          value: nameSpace.name
        }));

        return (
          <>
            <Row>
              <Col lg={2} className="no-padding">
                <Field
                  name="api_type"
                  component={YBFormSelect}
                  label="Select API type"
                  options={PITR_SUPPORTED_APIS.map((t) => {
                    return { value: BACKUP_API_TYPES[t], label: t };
                  })}
                  onChange={(_: any, val: any) => {
                    setFieldValue('api_type', val);
                    setFieldValue('database', null);
                  }}
                  components={{
                    IndicatorSeparator: null
                  }}
                  id="PitrApiTypeSelector"
                />
              </Col>
            </Row>
            <Row>
              <Col lg={6} className="no-padding">
                <Field
                  name="database"
                  component={YBFormSelect}
                  label="Select the Database you want to enable point-in-time recovery for"
                  options={nameSpacesList}
                  onChange={(_: any, val: any) => {
                    setFieldValue('database', val);
                  }}
                  components={{
                    IndicatorSeparator: null
                  }}
                  id="PitrDBNameSelector"
                />
              </Col>
            </Row>
            <Row>
              <div>Select the retention period</div>
              <Col lg={12} className="no-padding">
                <Row className="duration-options">
                  <Col lg={1} className="no-padding">
                    <Field
                      name="retention_interval"
                      component={YBNumericInput}
                      input={{
                        onChange: (val: number) => setFieldValue('retention_interval', val),
                        value: values['retention_interval'],
                        id: 'PitrRetentionPeriodInput'
                      }}
                      minVal={2}
                    />
                  </Col>
                  <Col lg={3}>Day(s)</Col>
                </Row>
              </Col>
              {errors['retention_interval'] && (
                <Col lg={12} className="no-padding help-block standard-error">
                  {errors['retention_interval']}
                </Col>
              )}
            </Row>
            <div className="notice">
              <b>Note:</b> the default backup interval is 24 hours
            </div>
          </>
        );
      }}
    />
  );
};
