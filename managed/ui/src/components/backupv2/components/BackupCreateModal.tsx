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
  YBFormToggle,
  YBNumericInput
} from '../../common/forms/fields';
import {
  BACKUP_API_TYPES,
  Backup_Options_Type,
  IBackupEditParams,
  IStorageConfig,
  ITable
} from '../common/IBackup';
import { useDispatch, useSelector } from 'react-redux';
import { find, flatten, groupBy, isArray, omit, uniq, uniqBy } from 'lodash';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { fetchTablesInUniverse } from '../../../actions/xClusterReplication';
import { YBLoading } from '../../common/indicators';
import { YBSearchInput } from '../../common/forms/fields/YBSearchInput';
import Bulb from '../../universes/images/bulb.svg';
import { toast } from 'react-toastify';
import { createBackup, editBackup } from '../common/BackupAPI';
import { Badge_Types, StatusBadge } from '../../common/badge/StatusBadge';
import { createBackupSchedule, editBackupSchedule } from '../common/BackupScheduleAPI';

import { IBackupSchedule } from '../common/IBackupSchedule';
import { MILLISECONDS_IN } from '../scheduled/ScheduledBackupUtils';
import { components } from 'react-select';


import { ParallelThreads } from '../common/BackupUtils';
import { isDefinedNotNull } from '../../../utils/ObjectUtils';
import { isYbcEnabledUniverse } from '../../../utils/UniverseUtils';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../actions/universe';
import { QUERY_KEY, api } from '../../../redesign/features/universe/universe-form/utils/api';
import { RunTimeConfigEntry } from '../../../redesign/features/universe/universe-form/utils/dto';
import { handleCACertErrMsg } from '../../customCACerts';
import './BackupCreateModal.scss';
import Close from '../../universes/images/close.svg';

interface BackupCreateModalProps {
  onHide: Function;
  visible: boolean;
  currentUniverseUUID: string | undefined;
  isScheduledBackup?: boolean;
  isEditMode?: boolean;
  editValues?: Record<string, any>;
  isIncrementalBackup?: boolean;
  isEditBackupMode?: boolean;
}

type ToogleScheduleProps = Partial<IBackupSchedule> & Pick<IBackupSchedule, 'scheduleUUID'>;

const DURATIONS = ['Days', 'Months', 'Years'];

const DEFAULT_MIN_INCREMENTAL_BACKUP_INTERVAL = 1800; //in secs

const TABLES_NOT_PRESENT_MSG = (api: string) => (
  <span className="alert-message warning">
    <i className="fa fa-warning" /> There are no {api} databases in this universe to backup.
  </span>
);

const CONFIG_DOESNT_SATISFY_NODES_MSG = () => (
  <span className="alert-message warning">
    <i className="fa fa-warning" />{' '}
    <span>
      <b>Warning!</b> This config does not have buckets for all regions that this universe has nodes
      in. This will lead to increased costs due to cross region data transfer and can result in
      violations related to data handling such as gdpr.{' '}
    </span>
  </span>
);

const DURATION_OPTIONS = DURATIONS.map((t: string) => {
  return {
    value: t,
    label: t
  };
});

const SCHEDULE_DURATION_OPTIONS = ['Minutes', 'Hours', ...DURATIONS].map((t: string) => {
  return {
    value: t,
    label: t
  };
});

const INCREMENTAL_BACKUP_DURATION_OPTIONS = ['Minutes', 'Hours', 'Days', 'Months'].map(
  (t: string) => {
    return {
      value: t,
      label: t
    };
  }
);

const TABLE_BACKUP_OPTIONS = [
  { label: 'Select all tables in this Keyspace', value: Backup_Options_Type.ALL },
  { label: 'Select a subset of tables', value: Backup_Options_Type.CUSTOM }
];

const STEPS = [
  {
    title: (
      isScheduledBackup: boolean,
      isEditMode: boolean,
      isIncrementalBackup = false,
      isEditBackupMode: boolean
    ) => {
      if (isEditBackupMode) {
        return 'Change Retention Period';
      }
      if (isScheduledBackup) {
        return `${isEditMode ? 'Edit' : 'Create'} scheduled backup policy`;
      }
      if (isIncrementalBackup) {
        return 'Create Incremental Backup';
      }
      return 'Backup Now';
    },
    submitLabel: (isScheduledBackup: boolean, isEditMode: boolean, isEditBackupMode: boolean) => {
      if (isScheduledBackup) {
        return isEditMode ? 'Apply Changes' : 'Create';
      } else {
        return isEditBackupMode ? 'Apply Changes' : 'Backup';
      }
    },
    component: BackupConfigurationForm,
    footer: () => null
  }
];

const initialValues = {
  scheduleName: '',
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
  parallel_threads: ParallelThreads.MIN,
  storage_config: null as any,
  is_incremental_backup_enabled: false,
  incremental_backup_frequency: 1,
  incremental_backup_frequency_type: INCREMENTAL_BACKUP_DURATION_OPTIONS[1],
  isTableByTableBackup: false,
  useTablespaces: false
};

export const BackupCreateModal: FC<BackupCreateModalProps> = ({
  onHide,
  visible,
  currentUniverseUUID,
  isScheduledBackup = false,
  isEditMode = false,
  editValues = {},
  isIncrementalBackup = false,
  isEditBackupMode = false
}) => {
  const [currentStep, setCurrentStep] = useState(0);

  const { data: tablesInUniverse, isLoading: isTableListLoading } = useQuery(
    [currentUniverseUUID, 'tables'],
    () => fetchTablesInUniverse(currentUniverseUUID!),
    {
      enabled: visible
    }
  );

  const { data: runtimeConfigs } = useQuery(
    [QUERY_KEY.fetchCustomerRunTimeConfigs],
    () => api.fetchRunTimeConfigs(true, currentUniverseUUID!),
    {
      enabled: visible
    }
  );

  const universeDetails = useSelector(
    (state: any) => state.universe?.currentUniverse?.data?.universeDetails
  );

  const nodesInRegionsList =
    uniq(flatten(universeDetails?.clusters.map((e: any) => e.regions.map((r: any) => r.code)))) ??
    [];

  const primaryCluster = find(universeDetails?.clusters, { clusterType: 'PRIMARY' });

  const minIncrementalScheduleFrequencyInSecs = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.backup.minIncrementalScheduleFrequencyInSecs'
  );

  initialValues['parallel_threads'] =
    Math.min(primaryCluster?.userIntent?.numNodes, ParallelThreads.MAX) || ParallelThreads.MIN;

  let isYbcEnabledinCurrentUniverse = false;

  if (isDefinedNotNull(currentUniverseUUID)) {
    isYbcEnabledinCurrentUniverse = isYbcEnabledUniverse(universeDetails);
  }

  const allowTableByTableBackup = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.backup.allow_table_by_table_backup_ycql'
  );

  const useTablespacesByDefault = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.backup.always_backup_tablespaces'
  );

  const queryClient = useQueryClient();
  const storageConfigs = useSelector((reduxState: any) => reduxState.customer.configs);
  const dispatch = useDispatch();

  const doCreateBackup = useMutation(
    (values: any) => {
      if (isYbcEnabledinCurrentUniverse) {
        values = omit(values, 'parallel_threads');
      }
      // if usetablespaces is enabled in runtime config, then send the "useTablespaces" as true
      if (useTablespacesByDefault?.value === 'true') {
        values['useTablespaces'] = true;
      }
      return createBackup(values, isIncrementalBackup);
    },
    {
      onSuccess: (resp, values) => {
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
        queryClient.invalidateQueries(['incremental_backups', values['baseBackupUUID']]);

        dispatch(fetchUniverseInfo(currentUniverseUUID) as any).then((response: any) => {
          dispatch(fetchUniverseInfoResponse(response.payload));
        });

        onHide();
      },
      onError: (err: any) => {
        onHide();
        !handleCACertErrMsg(err) && toast.error(err.response.data.error);
      }
    }
  );

  const doEditBackup = useMutation(
    (values: IBackupEditParams) => {
      return editBackup(values);
    },
    {
      onSuccess: (resp, values) => {
        onHide();
        toast.success('Backup Edited Successfully!');
      },
      onError: (err: any) => {
        onHide();
        !handleCACertErrMsg(err) && toast.error(err.response.data.error);
      }
    }
  );

  const doCreateBackupSchedule = useMutation(
    (values: any) => {
      if (isYbcEnabledinCurrentUniverse) {
        values = omit(values, 'parallel_threads');
      }
      if (useTablespacesByDefault?.value === 'true') {
        values['useTablespaces'] = true;
      }
      return createBackupSchedule(values);
    },
    {
      onSuccess: (resp) => {
        toast.success(
          <span>
            Creating schedule policy. Click &nbsp;
            <a href={`/tasks/${resp.data.taskUUID}`} target="_blank" rel="noopener noreferrer">
              here
            </a>
            &nbsp; for task details
          </span>
        );
        queryClient.invalidateQueries(['scheduled_backup_list']);
        onHide();
      },
      onError: (err: any) => {
        onHide();
        !handleCACertErrMsg(err) && toast.error(err?.response?.data?.error ?? 'An Error occurred');
      }
    }
  );

  const doEditBackupSchedule = useMutation(
    (val: ToogleScheduleProps) => {
      if (isYbcEnabledinCurrentUniverse) {
        val = omit(val, 'parallel_threads');
      }

      return editBackupSchedule(val);
    },
    {
      onSuccess: () => {
        toast.success(`Schedule policy is updated`);
        queryClient.invalidateQueries('scheduled_backup_list');
        onHide();
      },
      onError: (resp: any) => {
        onHide();
        !handleCACertErrMsg(resp) && toast.error(resp?.response?.data?.error ?? 'An error occurred');
      }
    }
  );

  const groupedStorageConfigs = useMemo(() => {
    if (!isArray(storageConfigs?.data)) {
      return [];
    }
    const filteredConfigs = storageConfigs.data.filter((c: IStorageConfig) => c.type === 'STORAGE');

    // if user has only one storage config, select it by default
    if (filteredConfigs.length === 1) {
      const { configUUID, configName, name } = filteredConfigs[0];
      initialValues['storage_config'] = { value: configUUID, label: configName, name: name };
    }

    const configs = filteredConfigs.map((c: IStorageConfig) => {
      return {
        value: c.configUUID,
        label: c.configName,
        name: c.name,
        regions: c.data?.REGION_LOCATIONS
      };
    });

    return Object.entries(groupBy(configs, (c: IStorageConfig) => c.name)).map(
      ([label, options]) => {
        return { label, options };
      }
    );
  }, [storageConfigs]);

  if (!visible) return null;

  const validationSchema = Yup.object().shape({
    scheduleName: Yup.string().when('storage_config', {
      is: () => isScheduledBackup && !isEditBackupMode,
      then: Yup.string().required('Required')
    }),
    // we don't support schedules backups less than an hour
    policy_interval: Yup.number().test({
      message: 'Interval should be greater than an hour',
      test: function (value) {
        if (this.parent.use_cron_expression || !isScheduledBackup) {
          return true;
        }
        return (
          value * MILLISECONDS_IN[this.parent.policy_interval_type.value.toUpperCase()] >=
          MILLISECONDS_IN['HOURS']
        );
      }
    }),
    cron_expression: Yup.string().when('use_cron_expression', {
      is: (use_cron_expression) => isScheduledBackup && use_cron_expression,
      then: Yup.string().required('Required')
    }),
    storage_config: Yup.object().nullable().required('Required'),
    db_to_backup: Yup.object().nullable().required('Required'),
    retention_interval: Yup.number().when('keep_indefinitely', {
      is: (keep_indefinitely) => !keep_indefinitely,
      then: Yup.number().min(1, 'Duration must be greater than or equal to one')
    }),
    parallel_threads: Yup.number().when('storage_config', {
      is: !isYbcEnabledinCurrentUniverse,
      then: Yup.number()
        .min(
          ParallelThreads.MIN,
          `Parallel threads should be greater than or equal to ${ParallelThreads.MIN}`
        )
        .max(
          ParallelThreads.MAX,
          `Parallel threads should be less than or equal to ${ParallelThreads.MAX}`
        )
    }),
    incremental_backup_frequency: Yup.number()
      .test({
        message: 'Incremental backup interval must be less than full backup',
        test: function (value) {
          if (
            !isScheduledBackup ||
            !this.parent.is_incremental_backup_enabled ||
            this.parent.use_cron_expression
          )
            return true;

          return (
            value *
            MILLISECONDS_IN[this.parent.incremental_backup_frequency_type.value.toUpperCase()] <
            this.parent.policy_interval *
            MILLISECONDS_IN[this.parent.policy_interval_type.value.toUpperCase()]
          );
        }
      })
      .test({
        message: `Incremental backup should be greater than ${Number(
          minIncrementalScheduleFrequencyInSecs?.value ?? DEFAULT_MIN_INCREMENTAL_BACKUP_INTERVAL
        ) / 60
          } minutes`,
        test: function (value) {
          if (minIncrementalScheduleFrequencyInSecs) {
            if (
              value *
              MILLISECONDS_IN[
              this.parent.incremental_backup_frequency_type.value.toUpperCase()
              ] >=
              parseInt(minIncrementalScheduleFrequencyInSecs.value) * 1000
            ) {
              return true;
            }
            return false;
          }
          return true;
        }
      })
  });

  return (
    <YBModalForm
      size="large"
      title={STEPS[currentStep].title(
        isScheduledBackup,
        isEditMode,
        isIncrementalBackup,
        isEditBackupMode
      )}
      className="backup-modal"
      visible={visible}
      validationSchema={validationSchema}
      showCancelButton={isScheduledBackup}
      onFormSubmit={async (
        values: any,
        { setSubmitting }: { setSubmitting: any; setFieldError: any }
      ) => {
        setSubmitting(false);

        if (!tablesInUniverse?.data.some((t: ITable) => t.tableType === values['api_type'].value)) {
          return;
        }

        if (isEditBackupMode) {
          const backup = values.backupObj;
          const currentTime = Date.now();
          const lastBackedUpTime = backup.hasIncrementalBackups
            ? Date.parse(backup.lastIncrementalBackupTime)
            : Date.parse(backup.commonBackupInfo.createTime);
          const retentionTimeUnit = values['retention_interval_type'].value.toUpperCase();
          const newExpirationTime =
            lastBackedUpTime + values['retention_interval'] * MILLISECONDS_IN[retentionTimeUnit];
          doEditBackup.mutateAsync({
            backupUUID: backup.backupUUID,
            timeBeforeDeleteFromPresentInMillis: values['keep_indefinitely']
              ? 0
              : newExpirationTime - currentTime,
            storageConfigUUID: '',
            expiryTimeUnit: retentionTimeUnit
          });
        } else if (isScheduledBackup) {
          if (isEditMode) {
            const editPayloadValues = {
              scheduleUUID: values.scheduleObj.scheduleUUID,
              status: values.scheduleObj.status
            };
            if (values.use_cron_expression) {
              editPayloadValues['cronExpression'] = values.cron_expression;
            } else {
              editPayloadValues['frequency'] =
                values['policy_interval'] *
                MILLISECONDS_IN[values['policy_interval_type'].value.toUpperCase()];
              editPayloadValues['frequencyTimeUnit'] = values[
                'policy_interval_type'
              ].value.toUpperCase();
            }
            if (values['is_incremental_backup_enabled']) {
              editPayloadValues['incrementalBackupFrequency'] =
                values['incremental_backup_frequency'] *
                MILLISECONDS_IN[values['incremental_backup_frequency_type'].value.toUpperCase()];
              editPayloadValues['incrementalBackupFrequencyTimeUnit'] = values[
                'incremental_backup_frequency_type'
              ].value.toUpperCase();
            }
            doEditBackupSchedule.mutateAsync(editPayloadValues);
          } else {
            doCreateBackupSchedule.mutateAsync({
              ...values,
              universeUUID: currentUniverseUUID,
              tablesList: tablesInUniverse?.data
            });
          }
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
      submitLabel={STEPS[currentStep].submitLabel(isScheduledBackup, isEditMode, isEditBackupMode)}
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
              isEditMode,
              nodesInRegionsList,
              isYbcEnabledinCurrentUniverse,
              isIncrementalBackup,
              isEditBackupMode,
              allowTableByTableBackup,
              useTablespacesByDefault
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
  isEditMode,
  nodesInRegionsList,
  isYbcEnabledinCurrentUniverse,
  isIncrementalBackup,
  isEditBackupMode,
  allowTableByTableBackup,
  useTablespacesByDefault
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
  nodesInRegionsList: string[];
  isYbcEnabledinCurrentUniverse: boolean;
  isIncrementalBackup: boolean;
  isEditBackupMode: boolean;
  allowTableByTableBackup: RunTimeConfigEntry;
  useTablespacesByDefault: RunTimeConfigEntry;
}) {
  const ALL_DB_OPTION = {
    label: `All ${values['api_type'].value === BACKUP_API_TYPES.YSQL ? 'Databases' : 'Keyspaces'}`,
    value: null
  };

  const isTableAvailableForBackup = tablesInUniverse?.some(
    (t: ITable) => t.tableType === values['api_type'].value
  );

  const tablesByAPI =
    tablesInUniverse?.filter((t: any) => t.tableType === values['api_type'].value) ?? [];

  const uniqueKeyspaces = uniqBy(tablesByAPI, 'keySpace').map((t: any) => {
    return {
      label: t.keySpace,
      value: t.keySpace
    };
  });

  let regions_satisfied_by_config = true;

  if (values['storage_config']?.regions?.length > 0) {
    regions_satisfied_by_config = nodesInRegionsList.every((e) =>
      find(values['storage_config'].regions, { REGION: e })
    );
  }
  return (
    <div className="backup-configuration-form">
      {isScheduledBackup && (
        <Row>
          <Col lg={8} className="no-padding">
            <Field
              name="scheduleName"
              component={YBFormInput}
              label="Policy Name"
              disabled={isEditMode}
            />
          </Col>
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
              setFieldValue('backup_tables', Backup_Options_Type.ALL);
              setFieldValue('selected_ycql_tables', []);
            }}
            isDisabled={isEditMode || isIncrementalBackup}
          />
        </Col>
        <Col lg={12} className="no-padding">
          {!isTableAvailableForBackup && TABLES_NOT_PRESENT_MSG(values['api_type'].label)}
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
              // eslint-disable-next-line react/display-name
              SingleValue: ({ data }: { data: any }) => (
                <>
                  <span className="storage-cfg-name">{data.label}</span>
                  <StatusBadge statusType={Badge_Types.DELETED} customLabel={data.name} />
                </>
              ),
              // eslint-disable-next-line react/display-name
              Option: (props: any) => {
                return (
                  <components.Option {...props}>
                    <div className="storage-cfg-select-label">{props.data.label}</div>
                    <div className="storage-cfg-select-meta">
                      <span>{`${props.data.name}${props.data.regions?.length > 0 ? ',' : ''
                        }`}</span>
                      {props.data.regions?.length > 0 && <span>Multi-region support</span>}
                    </div>
                  </components.Option>
                );
              }
            }}
            styles={{
              singleValue: (props: any) => {
                return { ...props, display: 'flex' };
              }
            }}
            isClearable
            isDisabled={isEditMode || isIncrementalBackup}
          />
          {!regions_satisfied_by_config && CONFIG_DOESNT_SATISFY_NODES_MSG()}
        </Col>
      </Row>
      <Row>
        <Col lg={8} className="no-padding">
          <Field
            name="db_to_backup"
            component={YBFormSelect}
            label="Select the Database you want to backup"
            options={[ALL_DB_OPTION, ...uniqueKeyspaces]}
            onChange={(_: any, val: any) => {
              setFieldValue('db_to_backup', val);
              setFieldValue('backup_tables', Backup_Options_Type.ALL);
              if (
                values['api_type'].value === BACKUP_API_TYPES.YCQL ||
                values['api_type'].value === BACKUP_API_TYPES.YEDIS
              ) {
                setFieldValue('selected_ycql_tables', []);
                //All keyspace selected
                if (val.value === null) {
                  setFieldValue('backup_tables', Backup_Options_Type.ALL);
                }
              }
            }}
            isDisabled={isEditMode || isIncrementalBackup}
          />
        </Col>
      </Row>
      {(values['api_type'].value === BACKUP_API_TYPES.YCQL ||
        values['api_type'].value === BACKUP_API_TYPES.YEDIS) && (
          <Row>
            <Col lg={12} className="no-padding">
              {TABLE_BACKUP_OPTIONS.map((target) => (
                <>
                  <label className="btn-group btn-group-radio" key={target.value}>
                    <Field
                      name="backup_tables"
                      component="input"
                      defaultChecked={values['backup_tables'] === target.value}
                      disabled={
                        values['db_to_backup'] === null ||
                        values['db_to_backup']?.value === null ||
                        isEditMode ||
                        isIncrementalBackup ||
                        isEditBackupMode
                      }
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
                            <i className="fa fa-pencil" />
                            &nbsp;
                            {`${isIncrementalBackup || isEditMode ? 'View' : 'Edit'} `} selection
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
      {allowTableByTableBackup?.value === 'true' && values['api_type'].value === BACKUP_API_TYPES.YCQL && (
        <Row>
          <Col>
            <Field
              name="isTableByTableBackup"
              component={YBCheckBox}
              disabled={isEditMode}
              checkState={values['isTableByTableBackup']}
            />
            Take table by table backup
          </Col>
        </Row>
      )}

      {
        useTablespacesByDefault?.value === 'false' && (
          <Row>
            <Col lg={8} className='no-padding tablespaces'>
              <div>
                <Field
                  name="useTablespaces"
                  component={YBCheckBox}
                  disabled={isEditMode && !isEditBackupMode}
                  checkState={values['useTablespaces']}
                />
                Backup tablespaces information
              </div>
              <div className='tablespaces-subText'>
                In Universes using tablespaces, this allows restoring while preserving source universe tablespaces. (Given both source and target have the same topology.)
              </div>
            </Col>
          </Row>
        )
      }

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
                readOnly={values['keep_indefinitely'] || (isEditMode && !isEditBackupMode)}
              />
            </Col>
            <Col lg={3}>
              <Field
                name="retention_interval_type"
                component={YBFormSelect}
                options={DURATION_OPTIONS}
                isDisabled={values['keep_indefinitely'] || (isEditMode && !isEditBackupMode)}
              />
            </Col>
            <Col lg={4}>
              <Field
                name="keep_indefinitely"
                component={YBCheckBox}
                disabled={isEditMode && !isEditBackupMode}
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
      {isScheduledBackup && !isEditBackupMode && (
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
                      value={values['policy_interval_type']}
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
            {errors['policy_interval'] && (
              <Col lg={12} className="no-padding help-block standard-error">
                {errors['policy_interval']}
              </Col>
            )}
          </Col>
          {errors['retention_interval'] && (
            <Col lg={12} className="no-padding help-block standard-error">
              {errors['retention_interval']}
            </Col>
          )}
        </Row>
      )}
      {isScheduledBackup && (
        <Row>
          <Col lg={8} className="incremental-backups">
            <div className="toggle-incremental-status">
              <Field
                name="is_incremental_backup_enabled"
                component={YBFormToggle}
                isReadOnly={isEditMode}
              />
              <span>Take incremental backups within full backup intervals</span>
            </div>
            {values.is_incremental_backup_enabled && (
              <>
                {!isEditMode && (
                  <Col lg={12} className="no-padding incremental-interval-info">
                    Interval must be less than &nbsp;
                    <b>
                      {values.policy_interval}&nbsp;{values.policy_interval_type.value}
                    </b>
                  </Col>
                )}

                <Col lg={12} className="no-padding">
                  <div>Set incremental backup intervals</div>
                  <div className="incremental-interval-ctrls">
                    <Col lg={2} className="no-padding">
                      <Field
                        name="incremental_backup_frequency"
                        component={YBNumericInput}
                        input={{
                          onChange: (val: number) =>
                            setFieldValue('incremental_backup_frequency', val),
                          value: values['incremental_backup_frequency']
                        }}
                        minVal={0}
                      />
                    </Col>
                    <Col lg={4}>
                      <Field
                        name="incremental_backup_frequency_type"
                        component={YBFormSelect}
                        options={INCREMENTAL_BACKUP_DURATION_OPTIONS}
                      />
                    </Col>
                  </div>
                </Col>
              </>
            )}
            {errors['incremental_backup_frequency'] && (
              <Col lg={12} className="no-padding help-block standard-error">
                {errors['incremental_backup_frequency']}
              </Col>
            )}
          </Col>
        </Row>
      )}
      {!isYbcEnabledinCurrentUniverse && (
        <Row>
          <Col lg={6} className="no-padding">
            <Field
              name="parallel_threads"
              component={YBNumericInput}
              input={{
                onChange: (val: number) => setFieldValue('parallel_threads', val),
                value: values['parallel_threads']
              }}
              minVal={1}
              label="Parallel threads (Optional)"
              readOnly={isEditMode || isIncrementalBackup}
            />
            {errors['parallel_threads'] && (
              <span className="standard-error">{errors['parallel_threads']}</span>
            )}
          </Col>
        </Row>
      )}
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
        isEditMode={isEditMode || isIncrementalBackup}
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
  isEditMode: boolean;
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
  setFieldValue,
  isEditMode
}) => {
  const tablesInKeyspaces = tablesList
    ?.filter((t) => t.tableType === values['api_type'].value && !t.isIndexTable)
    .filter(
      (t) => values['db_to_backup']?.value === null || t.keySpace === values['db_to_backup']?.value
    );
  return (
    <YBModalForm
      formName="alertDestinationForm"
      title={'Select Tables'}
      visible={visible}
      className="backup-modal"
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
                disabled={isEditMode || (tablesInKeyspaces ? tablesInKeyspaces.length === 0 : true)}
                btnText="Select all "
                onClick={() => {
                  setFieldValue('selected_ycql_tables', tablesInKeyspaces);
                }}
              />
            </Col>
            <Col lg={12} className="no-padding table-list">
              {tablesInKeyspaces
                ?.filter(
                  // eslint-disable-next-line @typescript-eslint/prefer-includes
                  (t) => t.tableName.toLowerCase().indexOf(values['search_text'].toLowerCase()) > -1
                )
                .filter((t) => !find(values['selected_ycql_tables'], { tableName: t.tableName }))
                // eslint-disable-next-line react/display-name
                .map((t) => {
                  return (
                    <div className="table-item" key={t.tableUUID}>
                      {t.tableName}
                      <span
                        className="select-icon"
                        onClick={() => {
                          if (isEditMode) return;
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
                      if (isEditMode) return;
                      setFieldValue(
                        'selected_ycql_tables',
                        values['selected_ycql_tables'].filter(
                          (f: ITable) => f.tableUUID !== t.tableUUID
                        )
                      );
                    }}
                  >
                    <img alt="Remove" src={Close} width="22" />
                  </span>
                </div>
              );
            })}
        </Col>
      </Row>
    </YBModalForm>
  );
};
