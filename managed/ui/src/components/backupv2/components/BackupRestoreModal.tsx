/*
 * Created on Mon Feb 28 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

//
//
//    Note:
//    If you change any functionality/api here, please make sure to change the same(if needed) in BackupAdvancedRestore.tsx too
//
//
//

import React, { FC, useState } from 'react';
import { findIndex, has } from 'lodash';
import { Alert, Col, Row } from 'react-bootstrap';
import { IBackup, ITable, IUniverse, Keyspace_Table, Backup_States, ICommonBackupInfo } from '..';
import { getKMSConfigs, restoreEntireBackup, fetchIncrementalBackup } from '../common/BackupAPI';
import { YBModalForm } from '../../common/forms';
import { KEYSPACE_VALIDATION_REGEX, SPINNER_ICON } from '../common/BackupUtils';

import { Field, FieldArray } from 'formik';
import { useMutation, useQuery } from 'react-query';
import { fetchTablesInUniverse, fetchUniversesList } from '../../../actions/xClusterReplication';
import { YBLoading } from '../../common/indicators';
import {
  YBButton,
  YBCheckBox,
  YBControlledNumericInputWithLabel,
  YBFormSelect,
  YBInputField
} from '../../common/forms/fields';
import * as Yup from 'yup';
import { toast } from 'react-toastify';
import { components } from 'react-select';
import clsx from 'clsx';
import { Badge_Types, StatusBadge } from '../../common/badge/StatusBadge';
import { YBSearchInput } from '../../common/forms/fields/YBSearchInput';
import { find, isFunction, omit } from 'lodash';
import { BACKUP_API_TYPES } from '../common/IBackup';
import { AllowedTasks, TableType } from '../../../redesign/helpers/dtos';
import { isActionFrozen } from '../../../redesign/helpers/utils';
import { isYbcEnabledUniverse } from '../../../utils/UniverseUtils';
import { isDefinedNotNull } from '../../../utils/ObjectUtils';
import { ybFormatDate } from '../../../redesign/helpers/DateUtils';
import { handleCACertErrMsg } from '../../customCACerts';
import { UNIVERSE_TASKS } from '../../../redesign/helpers/constants';

import './BackupRestoreModal.scss';

interface RestoreModalProps {
  backup_details: IBackup;
  onHide: Function;
  visible: boolean;
  allowedTasks: AllowedTasks;
  isRestoreEntireBackup?: boolean;
}

const TEXT_RESTORE = 'Restore';
const TEXT_RENAME_DATABASE = 'Next: Rename Databases/Keyspaces';
const RESTORE_YBC_BACKUP_TO_NON_BACKUP_UNIVERSE_MSG =
  'Cannot restore ybc backup to non-ybc universe';
const STEPS = [
  {
    title: 'Restore Backup',
    submitLabel: TEXT_RESTORE,
    component: RestoreChooseUniverseForm,
    footer: () => null
  },
  {
    title: 'Restore Backup',
    submitLabel: TEXT_RESTORE,
    component: RenameKeyspace,
    // eslint-disable-next-line react/display-name
    footer: (onClick: Function) => (
      <YBButton
        btnClass={`btn btn-default pull-right restore-wth-rename-but`}
        btnText="Back"
        onClick={onClick}
      />
    )
  }
];

const isYBCEnabledInUniverse = (universeList: IUniverse[], currentUniverseUUID: string) => {
  const universe = find(universeList, { universeUUID: currentUniverseUUID });
  return isYbcEnabledUniverse(universe?.universeDetails);
};

export const BackupRestoreModal: FC<RestoreModalProps> = ({
  backup_details,
  onHide,
  visible,
  allowedTasks,
  isRestoreEntireBackup = false
}) => {
  const [currentStep, setCurrentStep] = useState(0);
  const [isFetchingTables, setIsFetchingTables] = useState(false);
  const { data: incrementalBackups, isLoading: isIncBackupLoading, isError } = useQuery(
    ['incremental_backups', backup_details.commonBackupInfo.baseBackupUUID],
    () => fetchIncrementalBackup(backup_details.commonBackupInfo.baseBackupUUID),
    {
      enabled: backup_details?.hasIncrementalBackups
    }
  );

  const [overrideSubmitLabel, setOverrideSubmitLabel] = useState(TEXT_RESTORE);

  const { data: universeList, isLoading: isUniverseListLoading } = useQuery(['universe'], () =>
    fetchUniversesList().then((res) => res.data as IUniverse[])
  );

  const { data: kmsConfigs } = useQuery(['kms_configs'], () => getKMSConfigs());

  const kmsConfigList = kmsConfigs
    ? kmsConfigs.map((config: any) => {
        const labelName = config.metadata.provider + ' - ' + config.metadata.name;
        return { value: config.metadata.configUUID, label: labelName };
      })
    : [];

  const restore = useMutation(
    ({ backup_details, values }: { backup_details: IBackup; values: Record<string, any> }) => {
      const isYBCEnabledinTargetUniverse = isYBCEnabledInUniverse(
        universeList!,
        values['targetUniverseUUID'].value
      );
      if (backup_details.category === 'YB_CONTROLLER' && !isYBCEnabledinTargetUniverse) {
        toast.error(RESTORE_YBC_BACKUP_TO_NON_BACKUP_UNIVERSE_MSG);
        return Promise.reject(RESTORE_YBC_BACKUP_TO_NON_BACKUP_UNIVERSE_MSG);
      }

      if (isYBCEnabledinTargetUniverse && backup_details.category === 'YB_CONTROLLER') {
        values = omit(values, 'parallelThreads');
      }
      if (
        isRestoreEntireBackup &&
        backup_details?.hasIncrementalBackups &&
        incrementalBackups &&
        !isError
      ) {
        //Backend is already sending reponse in sorted order
        const recentBackup = incrementalBackups.data.filter(
          (e: ICommonBackupInfo) => e.state === Backup_States.COMPLETED
        )[0];
        return restoreEntireBackup({ ...backup_details, commonBackupInfo: recentBackup }, values);
      } else return restoreEntireBackup(backup_details, values);
    },
    {
      onSuccess: (resp) => {
        setCurrentStep(0);
        onHide();
        toast.success(
          <span>
            Success. Click &nbsp;
            <a href={`/tasks/${resp.data.taskUUID}`} target="_blank" rel="noopener noreferrer">
              here
            </a>
            &nbsp; for task details
          </span>
        );
      },
      onError: (resp: any) => {
        onHide();
        setCurrentStep(0);
        !handleCACertErrMsg(resp) && toast.error(resp.response.data.error);
      }
    }
  );

  const footerActions = [
    () => {},
    () => {
      setCurrentStep(currentStep - 1);
      setOverrideSubmitLabel(TEXT_RENAME_DATABASE);
    }
  ];

  if (isUniverseListLoading && isIncBackupLoading) {
    return <YBLoading />;
  }

  const initialValues = {
    targetUniverseUUID: undefined,
    parallelThreads: 1,
    backup: backup_details,
    keyspaces: Array(backup_details?.commonBackupInfo.responseList.length).fill(''),
    kmsConfigUUID:
      kmsConfigList.find(
        (config: any) => config.value === backup_details?.commonBackupInfo.kmsConfigUUID
      ) ?? null,
    should_rename_keyspace: false,
    disable_keyspace_rename: false,
    allow_YCQL_conflict_keyspace: false
  };

  const validateTablesAndRestore = async (
    values: any,
    options: {
      setFieldValue: Function;
      setFieldError?: Function;
      setSubmitting?: Function;
      doRestore: boolean;
    }
  ) => {
    // Restoring with duplicate keyspace name is supported in redis
    if (values['backup']['backupType'] === BACKUP_API_TYPES.YEDIS) {
      isFunction(options.setSubmitting) && options.setSubmitting(false);
      if (options.doRestore) {
        restore.mutate({ backup_details: backup_details as IBackup, values });
      }
      return;
    }
    // find if the user has entered the duplicate keyspace name
    // we could have just find uniq(user_entered_keyspace), but in table by table backup
    // we can have duplicate keyspace, so we make sure that single keyspace entered by user map with single keyspace
    // in the backup_details
    const renamedKeyspacemap = {};
    let hasduplicateName = false;
    values['keyspaces'].forEach((k: string, i: number) => {
      if (!k) return;
      const keyspaceToMatch = backup_details?.commonBackupInfo.responseList[i].keyspace;
      if (!renamedKeyspacemap[k]) {
        renamedKeyspacemap[k] = keyspaceToMatch;
      } else if (renamedKeyspacemap[k] !== keyspaceToMatch) {
        isFunction(options.setFieldError) &&
          options.setFieldError(`keyspaces[${i}]`, 'Duplicate keyspace name');
        hasduplicateName = true;
      }
    });
    // return if duplicate keyspace name is found.
    if (hasduplicateName) {
      isFunction(options.setSubmitting) && options.setSubmitting(false);
      return;
    }

    // for YCQL, we skip the table overwriding check in front end and defer it to the backend
    // see https://yugabyte.atlassian.net/browse/PLAT-6460
    if (values['backup']['backupType'] === BACKUP_API_TYPES.YCQL) {
      if (options.doRestore) {
        restore.mutate({
          backup_details: backup_details as IBackup,
          values
        });
        return;
      }

      options.setFieldValue('allow_YCQL_conflict_keyspace', true, false);
      options.setFieldValue('should_rename_keyspace', false, false);
      options.setFieldValue('disable_keyspace_rename', false, false);
      isFunction(options.setSubmitting) && options.setSubmitting(false);
      return;
    }

    setIsFetchingTables(true);
    options.setFieldValue('should_rename_keyspace', true, false);
    options.setFieldValue('disable_keyspace_rename', true, false);

    let fetchKeyspace: { data: ITable[] } = { data: [] };
    try {
      fetchKeyspace = await fetchTablesInUniverse(values['targetUniverseUUID'].value);
    } catch (ex) {
      setIsFetchingTables(false);
      toast.error(`unable to fetch database for "${values['targetUniverseUUID'].label}"`);
    }
    setIsFetchingTables(false);

    const keyspaceInForm = backup_details!.commonBackupInfo.responseList.map(
      (k, i) => values['keyspaces'][i] || k.keyspace
    );

    if (!Array.isArray(fetchKeyspace.data)) {
      toast.error('Unable to fetch tables from the universe. Choose a different universe', {
        className: 'toast-fetch-table-err-msg'
      });
      isFunction(options.setSubmitting) && options.setSubmitting(true);
      return;
    }
    const keyspaceInTargetUniverse = fetchKeyspace.data
      .filter((k) => k.tableType === values['backup']['backupType'])
      .map((k) => k.keySpace);
    let hasErrors = false;

    keyspaceInForm.forEach((k: string, index: number) => {
      if (keyspaceInTargetUniverse.includes(k)) {
        isFunction(options.setFieldError) &&
          options.setFieldError(`keyspaces[${index}]`, 'Name already exists in target universe');
        hasErrors = true;
      }
    });

    options.setFieldValue('should_rename_keyspace', hasErrors, false);
    options.setFieldValue('disable_keyspace_rename', hasErrors, false);

    if (hasErrors) {
      options.setFieldValue('searchText', '', false);
    }

    setOverrideSubmitLabel(hasErrors && currentStep === 0 ? TEXT_RENAME_DATABASE : TEXT_RESTORE);

    isFunction(options.setSubmitting) && options.setSubmitting(false);

    if (!hasErrors && options.doRestore) {
      restore.mutate({ backup_details: backup_details as IBackup, values });
    }
  };

  const validationSchema = Yup.object().shape({
    targetUniverseUUID: Yup.string().required('Target universe is required'),
    keyspaces:
      currentStep === 1
        ? Yup.array(
            Yup.string().matches(KEYSPACE_VALIDATION_REGEX, {
              message: 'Invalid keyspace name',
              excludeEmptyString: true
            })
          )
        : Yup.array(Yup.string()),
    parallelThreads: Yup.number()
      .min(1, 'Parallel threads should be greater than or equal to 1')
      .max(100, 'Parallel threads should be less than or equal to 100')
  });

  const isRestoreBackupTaskDisabled = isActionFrozen(allowedTasks, UNIVERSE_TASKS.RESTORE_BACKUP);

  return (
    <YBModalForm
      size="large"
      title={STEPS[currentStep].title}
      isButtonDisabled={currentStep === 1 && isRestoreBackupTaskDisabled}
      className="backup-modal"
      visible={visible}
      validationSchema={validationSchema}
      onFormSubmit={async (
        values: any,
        {
          setSubmitting,
          setFieldError,
          setFieldValue
        }: { setSubmitting: any; setFieldError: any; setFieldValue: Function }
      ) => {
        setSubmitting(false);
        if (values['should_rename_keyspace'] && currentStep !== STEPS.length - 1) {
          setCurrentStep(currentStep + 1);
          setOverrideSubmitLabel(TEXT_RESTORE);
        } else if (currentStep === STEPS.length - 1 || values['allow_YCQL_conflict_keyspace']) {
          await validateTablesAndRestore(values, {
            setFieldValue,
            setFieldError,
            setSubmitting,
            doRestore: true
          });
        } else {
          restore.mutate({ backup_details: backup_details as IBackup, values });
        }
      }}
      initialValues={initialValues}
      submitLabel={overrideSubmitLabel}
      headerClassName={clsx({
        'show-back-button': currentStep > 0
      })}
      showBackButton={currentStep > 0}
      backBtnCallbackFn={() => {
        setCurrentStep(currentStep - 1);
        setOverrideSubmitLabel(TEXT_RENAME_DATABASE);
      }}
      onHide={() => {
        setCurrentStep(0);
        onHide();
      }}
      pullRightFooter
      footerAccessory={STEPS[currentStep].footer(footerActions[currentStep])}
      render={(values: any) => (
        <>
          {isFetchingTables && (
            <Row>
              <Col lg={12} className="keyspace-loading no-padding">
                <Alert bsStyle="info">{SPINNER_ICON} Please wait. Doing pre-flight check</Alert>
              </Col>
            </Row>
          )}
          {STEPS[currentStep].component({
            ...values,
            backup_details,
            universeList,
            kmsConfigList,
            validateTablesAndRestore,
            setOverrideSubmitLabel
          })}
        </>
      )}
    ></YBModalForm>
  );
};

function RestoreChooseUniverseForm({
  backup_details,
  universeList,
  kmsConfigList,
  setFieldValue,
  values,
  validateTablesAndRestore,
  setOverrideSubmitLabel,
  setSubmitting,
  errors
}: {
  backup_details: IBackup;
  universeList: IUniverse[];
  kmsConfigList: Record<string, any>[];
  setFieldValue: Function;
  values: Record<string, any>;
  validateTablesAndRestore: Function;
  setOverrideSubmitLabel: Function;
  setSubmitting: Function;
  errors: Record<string, string>;
}) {
  let sourceUniverseNameAtFirst: IUniverse[] = [];

  const universe = find(universeList, { universeUUID: backup_details.universeUUID });
  let currentActiveKMS = '';
  if (universe && universe?.universeDetails?.encryptionAtRestConfig?.encryptionAtRestEnabled)
    currentActiveKMS = universe?.universeDetails?.encryptionAtRestConfig?.kmsConfigUUID;

  //kms config used in the universe while taking backup
  const isEncryptedBackup = has(backup_details.commonBackupInfo, 'kmsConfigUUID');
  const kmsIdDuringBackup = kmsConfigList.find(
    (config: Record<string, any>) => config?.value === backup_details.commonBackupInfo.kmsConfigUUID
  );
  if (kmsIdDuringBackup) {
    //move currently active kms to top of the list
    kmsConfigList = kmsConfigList.filter(
      (config: Record<string, any>) => config.value !== kmsIdDuringBackup.value
    );
    kmsConfigList.unshift(kmsIdDuringBackup);
  }

  if (universeList && universeList.length > 0) {
    sourceUniverseNameAtFirst = [...universeList.filter((u) => u.universeUUID)];
    const sourceUniverseIndex = universeList.findIndex(
      (u) => u.universeUUID === backup_details.universeUUID
    );
    if (sourceUniverseIndex) {
      const sourceUniverse = sourceUniverseNameAtFirst.splice(sourceUniverseIndex, 1);
      sourceUniverseNameAtFirst.unshift(sourceUniverse[0]);
    }
    sourceUniverseNameAtFirst = sourceUniverseNameAtFirst.filter(
      (u) => !u.universeDetails.universePaused
    );
  }

  let isYbcEnabledinCurrentUniverse = false;
  let showParallelThread = true;

  if (isDefinedNotNull(values['targetUniverseUUID']?.value)) {
    isYbcEnabledinCurrentUniverse = isYBCEnabledInUniverse(
      universeList,
      values['targetUniverseUUID']?.value
    );
  }

  if (isYbcEnabledinCurrentUniverse && backup_details.category === 'YB_CONTROLLER') {
    showParallelThread = false;
  }

  return (
    <div className="restore-choose-universe">
      <Row className="backup-info">
        <Col lg={6} className="no-padding">
          <div className="title">Backup Universe Name</div>
          <div>{backup_details.universeName}</div>
        </Col>
        <Col lg={6} className="no-padding align-right">
          <div className="title">Created at</div>
          {ybFormatDate(backup_details.commonBackupInfo.createTime)}
        </Col>
      </Row>
      <Row>
        <Col lg={12} className="no-padding">
          <h5>Restore to</h5>
        </Col>
      </Row>
      {backup_details.category === 'YB_CONTROLLER' &&
        isDefinedNotNull(values['targetUniverseUUID']?.value) &&
        !isYbcEnabledinCurrentUniverse && (
          <div>
            <Alert bsStyle="danger">{RESTORE_YBC_BACKUP_TO_NON_BACKUP_UNIVERSE_MSG}</Alert>
          </div>
        )}

      <Row>
        <Col lg={8} className="no-padding">
          <Field
            name="targetUniverseUUID"
            component={YBFormSelect}
            options={sourceUniverseNameAtFirst?.map((universe: IUniverse) => {
              return {
                label: universe.name,
                value: universe.universeUUID
              };
            })}
            components={{
              // eslint-disable-next-line react/display-name
              Option: (props: any) => {
                if (props.data.value === backup_details.universeUUID) {
                  return (
                    <components.Option {...props}>
                      {props.data.label}{' '}
                      <StatusBadge statusType={Badge_Types.DELETED} customLabel="Backup Source" />
                    </components.Option>
                  );
                }
                return <components.Option {...props} />;
              },
              SingleValue: ({ data }: { data: any }) => {
                if (data.value === backup_details.universeUUID) {
                  return (
                    <>
                      <span className="storage-cfg-name">{data.label}</span> &nbsp;
                      <StatusBadge statusType={Badge_Types.DELETED} customLabel={'Backup Source'} />
                    </>
                  );
                }
                return data.label;
              }
            }}
            styles={{
              singleValue: (props: any) => {
                return { ...props, display: 'flex' };
              }
            }}
            isClearable
            label="Select target universe name"
            onChange={(_: any, val: any) => {
              setFieldValue('targetUniverseUUID', val ?? undefined);
              if (!val) return;
              const targetUniverse = sourceUniverseNameAtFirst.find(
                (u) => u.universeUUID === val.value
              );
              if (targetUniverse) {
                const primaryCluster = find(targetUniverse.universeDetails?.clusters, {
                  clusterType: 'PRIMARY'
                });
                setFieldValue('parallelThreads', primaryCluster?.userIntent?.numNodes);
              }
              setSubmitting(true);
              validateTablesAndRestore(
                {
                  ...values,
                  targetUniverseUUID: val
                },
                {
                  setFieldValue,
                  setSubmitting
                }
              );
            }}
          />
        </Col>
      </Row>
      <Row>
        <Col lg={8} className="no-padding">
          <Field
            name="kmsConfigUUID"
            component={YBFormSelect}
            label={'KMS Configuration (Optional)'}
            options={kmsConfigList}
            components={{
              // eslint-disable-next-line react/display-name
              Option: (props: any) => {
                if (isEncryptedBackup && props.data.value === kmsIdDuringBackup?.value) {
                  return (
                    <components.Option {...props} className="active-kms">
                      <span className="kms-used">{props.data.label}</span>{' '}
                      <StatusBadge
                        statusType={Badge_Types.DELETED}
                        customLabel="Used during backup"
                      />{' '}
                      {props.data.value === currentActiveKMS && (
                        <StatusBadge statusType={Badge_Types.COMPLETED} customLabel="Active" />
                      )}
                    </components.Option>
                  );
                }
                return (
                  <components.Option {...props}>
                    <span>{props.data.label}</span>{' '}
                    {props.data.value === currentActiveKMS && (
                      <StatusBadge statusType={Badge_Types.COMPLETED} customLabel="Active" />
                    )}
                  </components.Option>
                );
              },
              SingleValue: ({ data }: { data: any }) => {
                if (isEncryptedBackup && data.value === kmsIdDuringBackup?.value) {
                  return (
                    <>
                      <span className="storage-cfg-name">{data.label}</span> &nbsp;
                      <StatusBadge
                        statusType={Badge_Types.DELETED}
                        customLabel="Used during backup"
                      />
                    </>
                  );
                }
                return data.label;
              }
            }}
            styles={{
              singleValue: (props: any) => {
                return { ...props, display: 'flex' };
              }
            }}
            isClearable
          />

          {isEncryptedBackup && !kmsIdDuringBackup ? (
            // kms used during backup is deleted
            <div className="deleted-kms-warning">
              <Alert bsStyle="warning" className="pre-provision-message">
                <div>
                  <i className="fa fa-exclamation-triangle warning-icon md-icon" /> &nbsp;
                </div>
                <div>
                  <div className="helper-text">
                    KMS configuration file for this backup has been deleted. In order to restore
                    <br /> this backup first create a KMS configuration with the exact settings.
                    Once
                    <br /> created, come back and select the newly created KMS configuration from
                    the
                    <br /> list above.
                  </div>
                  <br />
                  <div className="deleted-key-title">Deleted KMS configuration UUID:</div>
                  <div className="deleted-key">
                    {backup_details?.commonBackupInfo?.kmsConfigUUID}
                  </div>
                </div>
              </Alert>
            </div>
          ) : (
            <span className="kms-helper-text">
              For a successful restore, the KMS configuration used for restore should be the same{' '}
              <br />
              KMS configuration used during backup creation.
            </span>
          )}
        </Col>
      </Row>
      {backup_details.backupType !== TableType.REDIS_TABLE_TYPE && (
        <Row>
          <Col lg={12} className="should-rename-keyspace">
            <Field
              name="should_rename_keyspace"
              component={YBCheckBox}
              label={`Rename databases in this backup before restoring (${
                values['disable_keyspace_rename'] ? 'Required' : 'Optional'
              })`}
              input={{
                checked: values['should_rename_keyspace'],
                onChange: (event: React.ChangeEvent<HTMLInputElement>) => {
                  setFieldValue('should_rename_keyspace', event.target.checked);
                  setOverrideSubmitLabel(
                    event.target.checked ? TEXT_RENAME_DATABASE : TEXT_RESTORE
                  );
                }
              }}
              disabled={values['disable_keyspace_rename']}
            />
            {values['disable_keyspace_rename'] && (
              <div className="disable-keyspace-subtext">
                <b>Note!</b> This is required since there are databases with the same name in the
                selected target universe.
              </div>
            )}
          </Col>
        </Row>
      )}
      {showParallelThread && (
        <Row>
          <Col lg={8} className="no-padding">
            <Field
              name="parallelThreads"
              component={YBControlledNumericInputWithLabel}
              label="Parallel threads (Optional)"
              onInputChanged={(val: string) => setFieldValue('parallelThreads', parseInt(val))}
              val={values['parallelThreads']}
              minVal={1}
            />
            {errors['parallelThreads'] && (
              <span className="err-msg">{errors['parallelThreads']}</span>
            )}
          </Col>
        </Row>
      )}
    </div>
  );
}

export function RenameKeyspace({
  values,
  setFieldValue
}: {
  values: {
    backup: IBackup;
    [key: string]: any;
  };
  setFieldValue: Function;
}) {
  const isTableByTableBackup = values.backup.commonBackupInfo.tableByTableBackup;
  return (
    <div className="rename-keyspace-step">
      <Row>
        <Col lg={12} className="no-padding">
          <YBSearchInput
            val={values['searchText']}
            placeHolder="Search keyspace"
            onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
              setFieldValue('searchText', e.target.value);
            }}
          />
        </Col>
      </Row>
      <Row className="help-text">
        <Col lg={12} className="no-padding">
          Databases in this backup
        </Col>
      </Row>
      <FieldArray
        name="keyspaces"
        render={({ form: { errors } }) =>
          values.backup.commonBackupInfo.responseList.map(
            (keyspace: Keyspace_Table, index: number) =>
              (values['searchText'] &&
                keyspace.keyspace &&
                !keyspace.keyspace.includes(values['searchText'])) ||
              findIndex(values.backup.commonBackupInfo.responseList, {
                keyspace: keyspace.keyspace
              }) !== index ? null : (
                // eslint-disable-next-line react/jsx-indent
                <Row key={index}>
                  <Col lg={6} className="keyspaces-input no-padding">
                    <Field
                      name={`keyspaces[${index}]`}
                      component={YBInputField}
                      input={{
                        disabled: true,
                        value: keyspace.keyspace
                      }}
                    />
                    {errors['keyspaces']?.[index] && !values['keyspaces']?.[index] && (
                      <span className="err-msg">Name already exists. Rename to proceed</span>
                    )}
                  </Col>
                  <Col lg={6}>
                    <Field
                      name={`keyspaces[${index}]`}
                      component={YBInputField}
                      input={{
                        value: values['keyspaces'][`${index}`]
                      }}
                      onValueChanged={(val: any) => {
                        if (isTableByTableBackup) {
                          // if the tableByTable option is enabled, keyspaces with duplicate
                          // names can be present. So, we show unique keyspaces in the rename form.
                          // and update the new names for all the duplicate keyspaces
                          // See, https://yugabyte.atlassian.net/browse/PLAT-8319
                          values.backup.commonBackupInfo.responseList.forEach(
                            (table: any, i: number) => {
                              if (table.keyspace === keyspace.keyspace) {
                                setFieldValue(`keyspaces[${i}]`, val);
                              }
                            }
                          );
                        } else {
                          setFieldValue(`keyspaces[${index}]`, val);
                        }
                      }}
                      placeHolder="Add new name"
                    />
                    {errors['keyspaces']?.[index] && values['keyspaces']?.[index] && (
                      <span className="err-msg">{errors['keyspaces'][index]}</span>
                    )}
                  </Col>
                </Row>
              )
          )
        }
      />
    </div>
  );
}
