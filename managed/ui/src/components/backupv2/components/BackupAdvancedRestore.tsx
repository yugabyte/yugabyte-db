/*
 * Created on Mon May 09 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

// Advanced restore - used to restore backup from another platform

import { Field } from 'formik';
import { find, groupBy, omit } from 'lodash';
import React, { FC, useMemo, useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import { useMutation, useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { Badge_Types, StatusBadge } from '../../common/badge/StatusBadge';
import { YBModalForm } from '../../common/forms';
import {
  YBButton,
  YBCheckBox,
  YBControlledNumericInputWithLabel,
  YBFormInput,
  YBFormSelect,
  YBInputField
} from '../../common/forms/fields';
import * as Yup from 'yup';
import { getKMSConfigs, restoreEntireBackup } from '../common/BackupAPI';
import { BACKUP_API_TYPES, IBackup, IStorageConfig } from '../common/IBackup';
import { TableType } from '../../../redesign/helpers/dtos';

import { KEYSPACE_VALIDATION_REGEX, ParallelThreads } from '../common/BackupUtils';

import { toast } from 'react-toastify';
import { fetchTablesInUniverse } from '../../../actions/xClusterReplication';
import { YBLoading } from '../../common/indicators';
import clsx from 'clsx';

import { isDefinedNotNull } from '../../../utils/ObjectUtils';
import { isYbcEnabledUniverse } from '../../../utils/UniverseUtils';
import { handleCACertErrMsg } from '../../customCACerts';
import './BackupAdvancedRestore.scss';

const TEXT_RESTORE = 'Restore';

const STEPS = [
  {
    title: 'Restore Backup',
    submitLabel: 'Next: Rename Databases/Keyspaces',
    component: RestoreForm,
    footer: () => null
  },
  {
    title: 'Restore Backup',
    submitLabel: TEXT_RESTORE,
    component: RenameSingleKeyspace,
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

interface RestoreModalProps {
  onHide: Function;
  visible: boolean;
  currentUniverseUUID?: string;
}

const initialValues = {
  backup_location: '',
  api_type: { value: BACKUP_API_TYPES.YSQL, label: 'YSQL' },
  keyspace_name: '',
  storage_config: null as any,
  should_rename_keyspace: false,
  disable_keyspace_rename: false,
  parallelThreads: 1,
  kmsConfigUUID: null,
  new_keyspace_name: ''
};

export const BackupAdvancedRestore: FC<RestoreModalProps> = ({
  onHide,
  visible,
  currentUniverseUUID
}) => {
  const [currentStep, setCurrentStep] = useState(0);
  const storageConfigs = useSelector((reduxState: any) => reduxState.customer.configs);
  const [overrideSubmitLabel, setOverrideSubmitLabel] = useState<undefined | string>(TEXT_RESTORE);

  const { data: kmsConfigs } = useQuery(['kms_configs'], () => getKMSConfigs());

  const { data: tablesInUniverse, isLoading: isTableListLoading } = useQuery(
    [currentUniverseUUID, 'tables'],
    () => fetchTablesInUniverse(currentUniverseUUID!),
    {
      enabled: visible
    }
  );

  const universeDetails = useSelector(
    (state: any) => state.universe?.currentUniverse?.data?.universeDetails
  );

  let isYbcEnabledinCurrentUniverse = false;

  if (isDefinedNotNull(currentUniverseUUID)) {
    isYbcEnabledinCurrentUniverse = isYbcEnabledUniverse(universeDetails);
  }

  const restore = useMutation(
    ({ backup_details, values }: { backup_details: IBackup; values: Record<string, any> }) => {
      if (isYbcEnabledinCurrentUniverse) {
        values = omit(values, 'parallelThreads');
      }
      return restoreEntireBackup(backup_details, values);
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

  const primaryCluster = find(universeDetails?.clusters, { clusterType: 'PRIMARY' });

  initialValues['parallelThreads'] =
    Math.min(primaryCluster?.userIntent?.numNodes, ParallelThreads.MAX) || ParallelThreads.MIN;

  const kmsConfigList = kmsConfigs
    ? kmsConfigs.map((config: any) => {
        const labelName = config.metadata.provider + ' - ' + config.metadata.name;
        return { value: config.metadata.configUUID, label: labelName };
      })
    : [];

  const groupedStorageConfigs = useMemo(() => {
    // if user has only one storage config, select it by default
    if (storageConfigs?.data?.length === 1) {
      const { configUUID, configName, name } = storageConfigs.data[0];
      initialValues['storage_config'] = { value: configUUID, label: configName, name: name };
    }

    const configs = storageConfigs?.data
      ?.filter((c: IStorageConfig) => c.type === 'STORAGE')
      .map((c: IStorageConfig) => {
        return { value: c.configUUID, label: c.configName, name: c.name };
      });

    return Object.entries(groupBy(configs, (c: IStorageConfig) => c.name)).map(
      ([label, options]) => {
        return { label, options };
      }
    );
  }, [storageConfigs]);

  const validationSchema = Yup.object().shape({
    backup_location: Yup.string().required('Backup location is required'),
    storage_config: Yup.object().nullable().required('Required'),
    keyspace_name: Yup.string().when('api_type', {
      is: (api_type) => api_type.value !== BACKUP_API_TYPES.YEDIS,
      then: Yup.string().required('required')
    }),
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
      .min(
        ParallelThreads.MIN,
        `Parallel threads should be greater than or equal to ${ParallelThreads.MIN}`
      )
      .max(
        ParallelThreads.MAX,
        `Parallel threads should be less than or equal to ${ParallelThreads.MAX}`
      ),
    new_keyspace_name: Yup.string().when('should_rename_keyspace', {
      is: (should_rename_keyspace) => currentStep === STEPS.length - 1 && should_rename_keyspace,
      then: Yup.string().notOneOf([Yup.ref('keyspace_name')], 'Duplicate name')
    })
  });

  const doRestore = (
    values: typeof initialValues,
    setSubmitting: (isSubmitting: boolean) => void
  ) => {
    values['keyspaces'] = [
      values['should_rename_keyspace'] ? values['new_keyspace_name'] : values['keyspace_name']
    ];

    values['targetUniverseUUID'] = { value: currentUniverseUUID };

    const backup: Partial<IBackup & Record<string, any>> = {
      backupType: (values['api_type'].value as unknown) as TableType,
      commonBackupInfo: {
        storageConfigUUID: values['storage_config'].value,
        sse: values['storage_config'].name === 'S3',
        responseList: [
          {
            keyspace: values['should_rename_keyspace']
              ? values['new_keyspace_name']
              : values['keyspace_name'],
            tablesList: [],
            storageLocation: values['backup_location']
          }
        ]
      } as any
    };
    restore.mutate(
      { backup_details: backup as any, values },
      { onSettled: () => setSubmitting(false) }
    );
  };

  if (!visible) return null;

  return (
    <YBModalForm
      visible={visible}
      onHide={() => {
        setCurrentStep(0);
        onHide();
      }}
      className="backup-modal"
      title={'Advanced Restore'}
      initialValues={initialValues}
      validationSchema={validationSchema}
      dialogClassName="advanced-restore-modal"
      submitLabel={overrideSubmitLabel ?? STEPS[currentStep].submitLabel}
      onFormSubmit={(
        values: any,
        { setSubmitting }: { setSubmitting: (isSubmitting: boolean) => void }
      ) => {
        if (values['should_rename_keyspace'] && currentStep < STEPS.length - 1) {
          setCurrentStep(currentStep + 1);
          setSubmitting(false);
        } else {
          doRestore(values, setSubmitting);
        }
      }}
      headerClassName={clsx({
        'show-back-button': currentStep > 0
      })}
      showBackButton={currentStep > 0}
      backBtnCallbackFn={() => {
        setCurrentStep(currentStep - 1);
      }}
      render={(formikProps: any) =>
        isTableListLoading ? (
          <YBLoading />
        ) : (
          <>
            {STEPS[currentStep].component({
              ...formikProps,
              storageConfigs: groupedStorageConfigs,
              tablesInUniverse: tablesInUniverse?.data,
              kmsConfigList,
              setOverrideSubmitLabel,
              isYbcEnabledinCurrentUniverse
            })}
          </>
        )
      }
    />
  );
};

function RestoreForm({
  setFieldValue,
  storageConfigs,
  values,
  tablesInUniverse,
  setOverrideSubmitLabel,
  setSubmitting,
  errors,
  kmsConfigList,
  isYbcEnabledinCurrentUniverse
}: {
  setFieldValue: Function;
  values: Record<string, any>;
  tablesInUniverse: any;
  setOverrideSubmitLabel: Function;
  setSubmitting: Function;
  kmsConfigList: any;
  errors: Record<string, string>;
  storageConfigs: {
    label: string;
    value: {
      label: string;
      value: Partial<IStorageConfig>;
    };
  };
  isYbcEnabledinCurrentUniverse: boolean;
}) {
  return (
    <div className="advanced-restore-form">
      <Row>
        <Col lg={11} md={11} className="no-padding help-text">
          If you have more than one Yugabyte Platform installation you can use this form to restore
          a database/keyspace from a different Platform installation to this universe.
        </Col>
      </Row>
      <Row>
        <Col lg={12} md={12} className="no-padding help-text-2">
          Provide details for the database/keyspace you are trying to restore:
        </Col>
      </Row>
      <div className="restore-form-controls">
        <Row>
          <Col lg={4} className="no-padding">
            <Field
              name="api_type"
              component={YBFormSelect}
              label="API type"
              options={Object.keys(BACKUP_API_TYPES).map((t) => {
                return { value: BACKUP_API_TYPES[t], label: t };
              })}
              onChange={(_: any, val: any) => {
                setFieldValue('api_type', val);
                if (val.value === BACKUP_API_TYPES.YEDIS) {
                  setFieldValue('should_rename_keyspace', false);
                  setFieldValue('keyspace_name', '');
                  setOverrideSubmitLabel(TEXT_RESTORE);
                }
              }}
            />
          </Col>
        </Row>
        <Row>
          <Col lg={12} className="no-padding">
            <Field
              name="backup_location"
              component={YBFormInput}
              label="Backup location"
              placeholder="Backup location"
            />
          </Col>
        </Row>
        <Row>
          <Col lg={12} className="no-padding">
            <Field
              name="storage_config"
              component={YBFormSelect}
              label="Backup config"
              options={storageConfigs}
              components={{
                // eslint-disable-next-line react/display-name
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
              isClearable
            />
          </Col>
        </Row>
        {values['api_type'].value !== BACKUP_API_TYPES.YEDIS && (
          <Row>
            <Col lg={12} className="no-padding">
              <Field
                name="keyspace_name"
                component={YBFormInput}
                label={`${
                  values['api_type'].value === BACKUP_API_TYPES.YSQL ? 'Database' : 'Keyspace'
                } name`}
                placeholder={`${
                  values['api_type'].value === BACKUP_API_TYPES.YSQL ? 'Database' : 'Keyspace'
                } name`}
                validate={(name: string) => {
                  // Restoring with duplicate keyspace name is supported in redis and YCQL
                  if (
                    Array.isArray(tablesInUniverse) &&
                    values['api_type'].value === BACKUP_API_TYPES.YSQL &&
                    find(tablesInUniverse, { tableType: values['api_type'].value, keySpace: name })
                  ) {
                    setFieldValue('should_rename_keyspace', true, false);
                    setFieldValue('disable_keyspace_rename', true, false);
                    setOverrideSubmitLabel(undefined);
                  } else {
                    setFieldValue('disable_keyspace_rename', false, false);
                  }
                }}
              />
            </Col>
          </Row>
        )}
        {values['api_type'].value !== BACKUP_API_TYPES.YEDIS && (
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
                    setOverrideSubmitLabel(event.target.checked ? undefined : TEXT_RESTORE);
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
        <Row>
          <Col lg={12} className="no-padding">
            <Field
              name="kmsConfigUUID"
              component={YBFormSelect}
              label={'KMS Configuration (Optional)'}
              options={kmsConfigList}
              isClearable
            />
            <span className="kms-helper-text">
              For a successful restore, the KMS configuration used for restore should be the same{' '}
              <br />
              KMS configuration used during backup creation.
            </span>
          </Col>
        </Row>
      </div>
      {!isYbcEnabledinCurrentUniverse && (
        <Row>
          <Col lg={3} className="no-padding">
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

function RenameSingleKeyspace({
  values,
  setFieldValue
}: {
  values: Record<string, any>;
  setFieldValue: Function;
}) {
  return (
    <div className="rename-keyspace-step">
      <Row className="help-text">
        <Col lg={12} className="no-padding">
          Rename keyspace/database in this backup
        </Col>
      </Row>

      <Row>
        <Col lg={6} className="keyspaces-input no-padding">
          <Field
            name={`keyspace_name`}
            component={YBInputField}
            input={{
              disabled: true,
              value: values['keyspace_name']
            }}
          />
        </Col>
        <Col lg={6}>
          <Field
            name={`new_keyspace_name`}
            component={YBInputField}
            onValueChanged={(val: any) => setFieldValue(`new_keyspace_name`, val)}
          />
          {values['keyspace_name'] === values['new_keyspace_name'] && (
            <span className="err-msg">Duplicate name</span>
          )}
        </Col>
      </Row>
    </div>
  );
}
