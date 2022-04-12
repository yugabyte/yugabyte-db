/*
 * Created on Wed Mar 16 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useMemo, useState } from 'react';
import { FC } from 'react';
import { YBModalForm } from '../../common/forms';
import * as Yup from 'yup';
import { Col, Row } from 'react-bootstrap';
import { Field } from 'formik';
import {
  YBButton,
  YBCheckBox,
  YBFormInput,
  YBFormSelect,
  YBNumericInput
} from '../../common/forms/fields';
import { BACKUP_API_TYPES, Backup_Options_Type, IStorageConfig, ITable } from '../common/IBackup';
import { useSelector } from 'react-redux';
import { find, groupBy, uniqBy } from 'lodash';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { fetchTablesInUniverse } from '../../../actions/xClusterReplication';
import { YBLoading } from '../../common/indicators';
import { YBSearchInput } from '../../common/forms/fields/YBSearchInput';
import Bulb from '../../universes/images/bulb.svg';
import { toast } from 'react-toastify';
import { createBackup } from '../common/BackupAPI';
import { Badge_Types, StatusBadge } from '../../common/badge/StatusBadge';
import { createBackupSchedule } from '../common/BackupScheduleAPI';

import './BackupCreateModal.scss';

interface BackupCreateModalProps {
  onHide: Function;
  visible: boolean;
  currentUniverseUUID: string | undefined;
  isScheduledBackup?: boolean;
  isEditMode?: boolean;
  editValues?: Record<string, any>;
}

const DURATIONS = ['Days', 'Weeks', 'Months', 'Years'];

const DURATION_OPTIONS = DURATIONS.map((t: string) => {
  return {
    value: t,
    label: t
  };
});

const SCHEDULE_DURATION_OPTIONS = ['Mins', 'Hours', ...DURATIONS].map((t: string) => {
  return {
    value: t,
    label: t
  };
});

const TABLE_BACKUP_OPTIONS = [
  { label: 'Select all tables in this Keyspace', value: Backup_Options_Type.ALL },
  { label: 'Select a subset of tables', value: Backup_Options_Type.CUSTOM }
];

const STEPS = [
  {
    title: (isScheduledBackup: boolean) =>
      isScheduledBackup ? 'Create scheduled backup policy' : 'Backup Now',
    submitLabel: (isScheduledBackup: boolean) => (isScheduledBackup ? 'Create' : 'Backup'),
    component: BackupConfigurationForm,
    footer: () => null
  }
];

const validationSchema = Yup.object().shape({
  storage_config: Yup.object().required('Required'),
  db_to_backup: Yup.object().nullable().required('Required'),
  retention_interval: Yup.number().when('keep_indefinitely', {
    is: (keep_indefinitely) => !keep_indefinitely,
    then: Yup.number().min(1, 'Duration must be greater than or equal to one')
  })
});

const initialValues = {
  policy_name: '',
  policy_interval: 1,
  policy_interval_type: SCHEDULE_DURATION_OPTIONS[2], //default to days
  use_cron_expression: false,
  cron_expression: '',
  api_type: { value: BACKUP_API_TYPES.YSQL, label: 'YSQL' },
  backup_tables: Backup_Options_Type.ALL,
  retention_interval: 1,
  retention_interval_type: DURATION_OPTIONS[0],
  selected_ycql_tables: [],
  keep_indefinitely: false,
  search_text: '',
  parallel_threads: 8
};

export const BackupCreateModal: FC<BackupCreateModalProps> = ({
  onHide,
  visible,
  currentUniverseUUID,
  isScheduledBackup = false,
  isEditMode = false,
  editValues = {}
}) => {
  const [currentStep, setCurrentStep] = useState(0);

  const { data: tablesInUniverse, isLoading: isTableListLoading } = useQuery(
    [currentUniverseUUID, 'tables'],
    () => fetchTablesInUniverse(currentUniverseUUID!),
    {
      enabled: visible
    }
  );

  const queryClient = useQueryClient();
  const storageConfigs = useSelector((reduxState: any) => reduxState.customer.configs);

  const doCreateBackup = useMutation((values: any) => createBackup(values), {
    onSuccess: (resp) => {
      toast.success(
        <span>
          Backup is in progress. Click &nbsp;
          <a href={`/tasks/${resp.data.taskUUID}`} target="_blank" rel="noopener noreferrer">
            here
          </a>
          &nbsp; for task details
        </span>
      );
      queryClient.invalidateQueries(['backups']);
      onHide();
    },
    onError: (err: any) => {
      toast.error(err.response.data.error);
    }
  });

  const doCreateBackupSchedule = useMutation((values: any) => createBackupSchedule(values), {
    onSuccess: () => {
      toast.success('Schedule policy created');
      queryClient.invalidateQueries(['scheduled_backup_list']);
      onHide();
    },
    onError: (err: any) => {
      toast.error(err.data.error);
    }
  });

  const groupedStorageConfigs = useMemo(() => {
    const configs = storageConfigs.data
      .filter((c: IStorageConfig) => c.type === 'STORAGE')
      .map((c: IStorageConfig) => {
        return { value: c.configUUID, label: c.configName, name: c.name };
      });

    return Object.entries(groupBy(configs, (c: IStorageConfig) => c.name)).map(
      ([label, options]) => {
        return { label, options };
      }
    );
  }, [storageConfigs]);

  if (!visible) return null;

  return (
    <YBModalForm
      size="large"
      title={STEPS[currentStep].title(isScheduledBackup)}
      className="backup-create-modal"
      visible={visible}
      validationSchema={validationSchema}
      showCancelButton={isScheduledBackup}
      onFormSubmit={async (
        values: any,
        { setSubmitting }: { setSubmitting: any; setFieldError: any }
      ) => {
        setSubmitting(false);
        if (isScheduledBackup) {
          doCreateBackupSchedule.mutateAsync({
            ...values,
            universeUUID: currentUniverseUUID,
            tablesList: tablesInUniverse?.data
          });
        } else {
          doCreateBackup.mutateAsync({
            ...values,
            universeUUID: currentUniverseUUID,
            tablesList: tablesInUniverse?.data
          });
        }
      }}
      initialValues={{
        ...initialValues,
        ...editValues
      }}
      submitLabel={STEPS[currentStep].submitLabel(isScheduledBackup)}
      onHide={() => {
        setCurrentStep(0);
        onHide();
      }}
      pullRightFooter
      render={(values: any) =>
        isTableListLoading ? (
          <YBLoading />
        ) : (
          <>
            {STEPS[currentStep].component({
              ...values,
              isScheduledBackup,
              storageConfigs: groupedStorageConfigs,
              tablesInUniverse: tablesInUniverse?.data,
              isEditMode
            })}
          </>
        )
      }
    />
  );
};

function BackupConfigurationForm({
  kmsConfigList,
  setFieldValue,
  values,
  storageConfigs,
  tablesInUniverse,
  errors,
  isScheduledBackup,
  isEditMode
}: {
  kmsConfigList: any;
  setFieldValue: Function;
  values: Record<string, any>;
  tablesInUniverse: any;
  storageConfigs: {
    label: string;
    value: {
      label: string;
      value: Partial<IStorageConfig>;
    };
  };
  errors: Record<string, string>;
  isScheduledBackup: boolean;
  isEditMode: boolean;
}) {
  const ALL_DB_OPTION = {
    label: `All ${values['api_type'].value === BACKUP_API_TYPES.YSQL ? 'Databases' : 'Keyspaces'}`,
    value: null
  };

  return (
    <div className="backup-configuration-form">
      {isScheduledBackup && (
        <Row>
          <Col lg={8} className="no-padding">
            <Field
              name="policy_name"
              component={YBFormInput}
              label="Policy Name"
              disabled={isEditMode}
            />
          </Col>
        </Row>
      )}

      {isScheduledBackup && (
        <Row>
          <div>Set backup intervals</div>
          <Col lg={12} className="no-padding">
            <Row className="duration-options">
              {values['use_cron_expression'] ? (
                <Col lg={4} className="no-padding">
                  <Field name="cron_expression" component={YBFormInput} />
                </Col>
              ) : (
                <>
                  <Col lg={1} className="no-padding">
                    <Field
                      name="policy_interval"
                      component={YBNumericInput}
                      input={{
                        onChange: (val: number) => setFieldValue('policy_interval', val),
                        value: values['policy_interval']
                      }}
                      minVal={0}
                      readOnly={values['use_cron_expression']}
                    />
                  </Col>
                  <Col lg={3}>
                    <Field
                      name="policy_interval_type"
                      component={YBFormSelect}
                      options={SCHEDULE_DURATION_OPTIONS}
                      isDisabled={values['use_cron_expression']}
                    />
                  </Col>
                </>
              )}

              <Col lg={4}>
                <Field
                  name="use_cron_expression"
                  component={YBCheckBox}
                  checkState={values['use_cron_expression']}
                />
                Use cron expression (UTC)
              </Col>
            </Row>
          </Col>
          {errors['retention_interval'] && (
            <Col lg={12} className="no-padding help-block standard-error">
              {errors['retention_interval']}
            </Col>
          )}
        </Row>
      )}

      <Row>
        <Col lg={2} className="no-padding">
          <Field
            name="api_type"
            component={YBFormSelect}
            label="Select API type"
            options={Object.keys(BACKUP_API_TYPES).map((t) => {
              return { value: BACKUP_API_TYPES[t], label: t };
            })}
            onChange={(_: any, val: any) => {
              setFieldValue('api_type', val);
              setFieldValue('db_to_backup', null);
            }}
            isDisabled={isEditMode}
          />
        </Col>
      </Row>
      <Row>
        <Col lg={8} className="no-padding">
          <Field
            name="storage_config"
            component={YBFormSelect}
            label="Select the storage config you want to use for your backup"
            options={storageConfigs}
            components={{
              SingleValue: ({ data }: { data: any }) => (
                <>
                  <span className="storage-cfg-name">{data.label}</span>
                  <StatusBadge statusType={Badge_Types.DELETED} customLabel={data.name} />
                </>
              )
            }}
            styles={{
              singleValue: (props: any) => {
                return { ...props, display: 'flex' };
              }
            }}
            isDisabled={isEditMode}
          />
        </Col>
      </Row>
      <Row>
        <Col lg={8} className="no-padding">
          <Field
            name="db_to_backup"
            component={YBFormSelect}
            label="Select the Database you want to backup"
            options={[
              ALL_DB_OPTION,
              ...uniqBy(tablesInUniverse, 'keySpace')
                .filter((t: any) => t.tableType === values['api_type'].value)
                .map((t: any) => {
                  return {
                    label: t.keySpace,
                    value: t.keySpace
                  };
                })
            ]}
            onChange={(_: any, val: any) => {
              setFieldValue('db_to_backup', val);
              if (values['api_type'].value === BACKUP_API_TYPES.YCQL) {
                setFieldValue('selected_ycql_tables', []);
                //All keyspace selected
                if (val.value === null) {
                  setFieldValue('backup_tables', Backup_Options_Type.ALL);
                }
              }
            }}
            isDisabled={isEditMode}
          />
        </Col>
      </Row>
      {values['api_type'].value === BACKUP_API_TYPES.YCQL && (
        <Row>
          <Col lg={12} className="no-padding">
            {TABLE_BACKUP_OPTIONS.map((target) => (
              <>
                <label className="btn-group btn-group-radio" key={target.value}>
                  <Field
                    name="backup_tables"
                    component="input"
                    defaultChecked={values['backup_tables'] === target.value}
                    disabled={values['db_to_backup']?.value === null || isEditMode}
                    onChange={(e: React.ChangeEvent<HTMLInputElement>) => {
                      setFieldValue('backup_tables', e.target.value, false);
                      if (
                        e.target.value === Backup_Options_Type.CUSTOM &&
                        values['selected_ycql_tables'].length === 0
                      ) {
                        setFieldValue('show_select_ycql_table', true);
                      }
                    }}
                    checked={values['backup_tables'] === target.value}
                    type="radio"
                    value={target.value}
                  />
                  {target.label}
                  {target.value === Backup_Options_Type.CUSTOM &&
                    values['backup_tables'] === Backup_Options_Type.CUSTOM && (
                      <span className="tables-count">
                        <span>{values['selected_ycql_tables'].length} tables selected</span>
                        <span
                          className="edit-selection"
                          onClick={() => {
                            setFieldValue('show_select_ycql_table', true);
                          }}
                        >
                          <i className="fa fa-pencil" /> Edit selection
                        </span>
                      </span>
                    )}
                </label>
                <br />
              </>
            ))}
          </Col>
        </Row>
      )}

      <Row>
        <div>Select backup retention period</div>
        <Col lg={12} className="no-padding">
          <Row className="duration-options">
            <Col lg={1} className="no-padding">
              <Field
                name="retention_interval"
                component={YBNumericInput}
                input={{
                  onChange: (val: number) => setFieldValue('retention_interval', val),
                  value: values['retention_interval']
                }}
                minVal={0}
                readOnly={values['keep_indefinitely'] || isEditMode}
              />
            </Col>
            <Col lg={3}>
              <Field
                name="retention_interval_type"
                component={YBFormSelect}
                options={DURATION_OPTIONS}
                isDisabled={values['keep_indefinitely'] || isEditMode}
              />
            </Col>
            <Col lg={4}>
              <Field
                name="keep_indefinitely"
                component={YBCheckBox}
                disabled={isEditMode}
                checkState={values['keep_indefinitely']}
              />
              Keep indefinitely
            </Col>
          </Row>
        </Col>
        {errors['retention_interval'] && (
          <Col lg={12} className="no-padding help-block standard-error">
            {errors['retention_interval']}
          </Col>
        )}
      </Row>
      <Row>
        <Col lg={4} className="no-padding">
          <Field
            name="parallel_threads"
            component={YBNumericInput}
            input={{
              onChange: (val: number) => setFieldValue('parallel_threads', val),
              value: values['parallel_threads']
            }}
            minVal={initialValues['parallel_threads']}
            label="Parallel threads (Optional)"
            readOnly={isEditMode}
          />
        </Col>
      </Row>
      <SelectYCQLTablesModal
        tablesList={tablesInUniverse}
        visible={values['show_select_ycql_table']}
        onHide={() => {
          if (values['selected_ycql_tables'].length === 0) {
            setFieldValue('backup_tables', Backup_Options_Type.ALL);
          }
          setFieldValue('show_select_ycql_table', false);
        }}
        setFieldValue={setFieldValue}
        values={values}
      />
    </div>
  );
}

interface SelectYCQLTablesModalProps {
  tablesList: ITable[] | undefined;
  visible: boolean;
  onHide: () => void;
  values: Record<string, any>;
  setFieldValue: Function;
}

const infoText = (
  <div className="info-msg">
    <img alt="--" src={Bulb} width="24" />
    &nbsp;
    <span>Selected tables will appear here</span>
  </div>
);

export const SelectYCQLTablesModal: FC<SelectYCQLTablesModalProps> = ({
  tablesList,
  visible,
  onHide,
  values,
  setFieldValue
}) => {
  const tablesInKeyspaces = tablesList
    ?.filter((t) => t.tableType === BACKUP_API_TYPES.YCQL)
    .filter(
      (t) => values['db_to_backup']?.value === null || t.keySpace === values['db_to_backup']?.value
    );

  return (
    <YBModalForm
      formName="alertDestinationForm"
      title={'Select Tables'}
      visible={visible}
      onHide={onHide}
      submitLabel="Confirm"
      onFormSubmit={(_values: any, { setSubmitting }: { setSubmitting: any }) => {
        setSubmitting(false);
        if (values['selected_ycql_tables'].length === 0) {
          toast.error('No tables selected');
          return;
        }
        onHide();
      }}
      dialogClassName="select-ycql-modal"
    >
      <Row className="select-ycql-tables-modal">
        <Col lg={6} className="table-list-panel">
          <Row>
            <Col lg={12} className="no-padding">
              <YBSearchInput
                placeHolder="Search table name"
                onEnterPressed={(val: string) => setFieldValue('search_text', val)}
              />
            </Col>
            <Col lg={12} className="no-padding select-all">
              <span>Click to select the tables you want to backup</span>
              <YBButton
                disabled={tablesInKeyspaces ? tablesInKeyspaces.length === 0 : true}
                btnText="Select all "
                onClick={() => {
                  setFieldValue('selected_ycql_tables', tablesInKeyspaces);
                }}
              />
            </Col>
            <Col lg={12} className="no-padding table-list">
              {tablesInKeyspaces
                ?.filter(
                  (t) => t.tableName.toLowerCase().indexOf(values['search_text'].toLowerCase()) > -1
                )
                .filter((t) => !find(values['selected_ycql_tables'], { tableName: t.tableName }))
                .map((t) => {
                  return (
                    <div className="table-item" key={t.tableUUID}>
                      {t.tableName}
                      <span
                        className="select-icon"
                        onClick={() => {
                          setFieldValue('selected_ycql_tables', [
                            ...values['selected_ycql_tables'],
                            t
                          ]);
                        }}
                      >
                        <i className="fa fa-plus-circle" aria-hidden="true" />
                        Select
                      </span>
                    </div>
                  );
                })}
            </Col>
          </Row>
        </Col>
        <Col lg={6} className="selected-tables-list">
          {values['selected_ycql_tables'].length === 0
            ? infoText
            : values['selected_ycql_tables'].map((t: ITable) => {
                return (
                  <div className="selected-table-item" key={t.tableUUID}>
                    {t.tableName}
                    <span
                      className="remove-selected-table"
                      onClick={() => {
                        setFieldValue(
                          'selected_ycql_tables',
                          values['selected_ycql_tables'].filter(
                            (f: ITable) => f.tableUUID !== t.tableUUID
                          )
                        );
                      }}
                    >
                      X
                    </span>
                  </div>
                );
              })}
        </Col>
      </Row>
    </YBModalForm>
  );
};
