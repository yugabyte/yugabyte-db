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
import { YBButton, YBCheckBox, YBFormSelect, YBNumericInput } from '../../common/forms/fields';
import { BACKUP_API_TYPES, Backup_Options_Type, IStorageConfig, ITable } from '../common/IBackup';
import { useSelector } from 'react-redux';
import { find, groupBy, uniqBy } from 'lodash';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { fetchTablesInUniverse } from '../../../actions/xClusterReplication';
import { YBLoading } from '../../common/indicators';
import './BackupCreateModal.scss';
import { YBSearchInput } from '../../common/forms/fields/YBSearchInput';
import Bulb from '../../universes/images/bulb.svg';
import { toast } from 'react-toastify';
import { createBackup } from '../common/BackupAPI';
import { Badge_Types, StatusBadge } from '../../common/badge/StatusBadge';
interface BackupCreateModalProps {
  onHide: Function;
  visible: boolean;
  currentUniverseUUID: string | undefined;
}

const DURATION_OPTIONS = ['Days', 'Months', 'Weeks', 'Years'].map((t: string) => {
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
    title: 'Backup Now',
    submitLabel: 'Backup',
    component: BackupConfigurationForm,
    footer: () => null
  }
];

const validationSchema = Yup.object().shape({
  storage_config: Yup.object().required('Required'),
  db_to_backup: Yup.object().nullable().required('Required'),
  duration_period: Yup.number().when('keep_indefinitely', {
    is: (keep_indefinitely) => !keep_indefinitely,
    then: Yup.number().min(1, 'Duration must be greater than or equal to one')
  })
});
const initialValues = {
  api_type: { value: BACKUP_API_TYPES.YSQL, label: 'YSQL' },
  backup_tables: Backup_Options_Type.ALL,
  duration_period: 1,
  duration_type: DURATION_OPTIONS[0],
  selected_ycql_tables: [],
  keep_indefinitely: false,
  search_text: '',
  parallel_threads: 8
};

export const BackupCreateModal: FC<BackupCreateModalProps> = ({
  onHide,
  visible,
  currentUniverseUUID
}) => {
  const [currentStep, setCurrentStep] = useState(0);

  const { data: tablesInUniverse, isLoading: isTableListLoading } = useQuery(
    [currentUniverseUUID, 'tables'],
    () => fetchTablesInUniverse(currentUniverseUUID!)
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
  const groupedStorageConfigs = useMemo(() => {
    const configs = storageConfigs.data.map((c: IStorageConfig) => {
      return { value: c.configUUID, label: c.configName, name: c.name };
    });

    return Object.entries(groupBy(configs, (c: IStorageConfig) => c.name)).map(
      ([label, options]) => {
        return { label, options };
      }
    );
  }, [storageConfigs]);

  return (
    <YBModalForm
      size="large"
      title={STEPS[currentStep].title}
      className="backup-create-modal"
      visible={visible}
      validationSchema={validationSchema}
      onFormSubmit={async (
        values: any,
        { setSubmitting }: { setSubmitting: any; setFieldError: any }
      ) => {
        setSubmitting(false);
        doCreateBackup.mutateAsync({
          ...values,
          universeUUID: currentUniverseUUID,
          tablesList: tablesInUniverse?.data
        });
      }}
      initialValues={initialValues}
      submitLabel={STEPS[currentStep].submitLabel}
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
              storageConfigs: groupedStorageConfigs,
              tablesInUniverse: tablesInUniverse?.data
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
  errors
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
}) {
  const ALL_DB_OPTION = {
    label: `All ${values['api_type'].value === BACKUP_API_TYPES.YSQL ? 'Databases' : 'Keyspaces'}`,
    value: null
  };
  return (
    <div className="backup-configuration-form">
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
              }
            }}
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
                name="duration_period"
                component={YBNumericInput}
                input={{
                  onChange: (val: number) => setFieldValue('duration_period', val),
                  value: values['duration_period']
                }}
                minVal={0}
                readOnly={values['keep_indefinitely']}
              />
            </Col>
            <Col lg={3}>
              <Field
                name="duration_type"
                component={YBFormSelect}
                options={DURATION_OPTIONS}
                isDisabled={values['keep_indefinitely']}
              />
            </Col>
            <Col lg={4}>
              <Field name="keep_indefinitely" component={YBCheckBox} />
              Keep indefinitely
            </Col>
          </Row>
        </Col>
        {errors['duration_period'] && (
          <Col lg={12} className="no-padding help-block standard-error">
            {errors['duration_period']}
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
            label="Parallele threads (Optional)"
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
