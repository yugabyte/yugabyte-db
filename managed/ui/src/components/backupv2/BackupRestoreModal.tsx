/*
 * Created on Mon Feb 28 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useState } from 'react';
import { Alert, Col, Row } from 'react-bootstrap';
import { getKMSConfigs, IBackup, IUniverse, Keyspace_Table, restoreEntireBackup } from '.';
import { YBModalForm } from '../common/forms';
import {
  FormatUnixTimeStampTimeToTimezone,
  KEYSPACE_VALIDATION_REGEX,
  SearchInput,
  SPINNER_ICON
} from './BackupUtils';

import { Field, FieldArray } from 'formik';
import { useMutation, useQuery } from 'react-query';
import { fetchTablesInUniverse, fetchUniversesList } from '../../actions/xClusterReplication';
import { YBLoading } from '../common/indicators';
import {
  YBButton,
  YBControlledNumericInputWithLabel,
  YBFormSelect,
  YBInputField
} from '../common/forms/fields';
import * as Yup from 'yup';
import { toast } from 'react-toastify';
import { components } from 'react-select';
import { Badge_Types, StatusBadge } from '../common/badge/StatusBadge';
import './BackupRestoreModal.scss';

interface RestoreModalProps {
  backup_details: IBackup | null;
  onHide: Function;
  visible: boolean;
}

const STEPS = [
  {
    title: 'Restore Entire Backup',
    submitLabel: 'Next: Rename Databases/Keyspaces',
    component: RestoreChooseUniverseForm,
    footer: () => null
  },
  {
    title: 'Restore Entire Backup',
    submitLabel: 'Restore',
    component: RenameKeyspace,
    footer: (onClick: Function) => (
      <YBButton
        btnClass={`btn btn-default pull-right restore-wth-rename-but`}
        btnText="Back"
        onClick={onClick}
      />
    )
  }
];

export const BackupRestoreModal: FC<RestoreModalProps> = ({ backup_details, onHide, visible }) => {
  const [currentStep, setCurrentStep] = useState(0);
  const [showWithoutRenameModal, setShowWithoutRenameModal] = useState(false);
  const [isFetchingTables, setIsFetchingTables] = useState(false);

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
    ({ backup_details, values }: { backup_details: IBackup; values: Record<string, any> }) =>
      restoreEntireBackup(backup_details, values),
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
        setCurrentStep(0);
        toast.error(resp.response.data.error);
      }
    }
  );

  const footerActions = [
    () => setShowWithoutRenameModal(true),
    () => setCurrentStep(currentStep - 1)
  ];

  if (isUniverseListLoading) {
    return <YBLoading />;
  }

  const initialValues = {
    targetUniverseUUID: undefined,
    parallelThreads: 0,
    backup: backup_details,
    keyspaces: Array(backup_details?.responseList.length).fill(''),
    kmsConfigUUID: null
  };

  const validateTablesAndRestore = async (values: any, setFieldError: Function) => {
    setIsFetchingTables(true);
    const fetchKeyspace = await fetchTablesInUniverse(values['targetUniverseUUID'].value);
    setIsFetchingTables(false);
    const keyspaceInForm = backup_details!.responseList.map(
      (k, i) => values['keyspaces'][i] || k.keyspace
    );

    const keyspaceInTargetUniverse = fetchKeyspace.data.map((k: any) => k.keySpace);
    let hasErrors = false;
    keyspaceInForm.forEach((k: string, index: number) => {
      if (keyspaceInTargetUniverse.includes(k)) {
        setFieldError(`keyspaces[${index}]`, 'Name already exists in target universe');
        hasErrors = true;
      }
    });
    if (!hasErrors) {
      restore.mutate({ backup_details: backup_details as IBackup, values });
    }
  };

  const validationSchema = Yup.object().shape({
    targetUniverseUUID: Yup.string().required('Target universe UUID is required'),
    keyspaces:
      currentStep === 1
        ? Yup.array(
            Yup.string().matches(KEYSPACE_VALIDATION_REGEX, {
              message: 'Invalid keyspace name',
              excludeEmptyString: true
            })
          )
        : Yup.array(Yup.string())
  });

  return (
    <YBModalForm
      size="large"
      title={STEPS[currentStep].title}
      className="backup-modal"
      visible={visible}
      validationSchema={validationSchema}
      onFormSubmit={async (
        values: any,
        { setSubmitting, setFieldError }: { setSubmitting: any; setFieldError: any }
      ) => {
        setSubmitting(false);
        if (currentStep !== STEPS.length - 1) {
          setCurrentStep(currentStep + 1);
        } else {
          await validateTablesAndRestore(values, setFieldError);
        }
      }}
      initialValues={initialValues}
      submitLabel={STEPS[currentStep].submitLabel}
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
              <Col lg={12} className="keyspace-loading">
                <Alert bsStyle="info">
                  {SPINNER_ICON} Keyspaces are being fetched. Please wait
                </Alert>
              </Col>
            </Row>
          )}
          {STEPS[currentStep].component({
            ...values,
            backup_details,
            universeList,
            kmsConfigList
          })}
          <RestoreWithoutRenameModal
            visible={showWithoutRenameModal}
            onHide={() => {
              setShowWithoutRenameModal(false);
            }}
            onOverride={() => {
              restore.mutate({
                backup_details: backup_details as IBackup,
                values: values['values']
              });
            }}
            onSubmit={() => {
              setShowWithoutRenameModal(false);
            }}
          />
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
  values
}: {
  backup_details: IBackup;
  universeList: IUniverse[];
  kmsConfigList: any;
  setFieldValue: Function;
  values: Record<string, any>;
}) {
  let sourceUniverseNameAtFirst: IUniverse[] = [];
  if (universeList) {
    sourceUniverseNameAtFirst = [...universeList.filter((u) => u.universeUUID)];
    const sourceUniverseIndex = universeList.findIndex(
      (u) => u.universeUUID === backup_details.universeUUID
    );
    if (sourceUniverseIndex) {
      const sourceUniverse = sourceUniverseNameAtFirst.splice(sourceUniverseIndex, 1);
      sourceUniverseNameAtFirst.unshift(sourceUniverse[0]);
    }
  }
  return (
    <div className="restore-choose-universe">
      <Row>
        <Col lg={12} className="no-padding">
          Backup details
        </Col>
      </Row>
      <Row className="backup-info">
        <Col lg={6} className="no-padding">
          <div className="title">Backup Universe Name</div>
          <div>{backup_details.universeName}</div>
        </Col>
        <Col lg={6} className="no-padding align-right">
          <div className="title">Created at</div>
          <FormatUnixTimeStampTimeToTimezone timestamp={backup_details.createTime} />
        </Col>
      </Row>
      <Row>
        <Col lg={12}>
          <h5>Restore To</h5>
        </Col>
      </Row>
      <Row>
        <Col lg={8}>
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
              }
            }}
            label="Select target universe name"
          />
        </Col>
      </Row>
      <Row>
        <Col lg={8}>
          <Field
            name="parallelThreads"
            component={YBControlledNumericInputWithLabel}
            label="Parallel threads (Optional)"
            onInputChanged={(val: string) => setFieldValue('parallelThreads', parseInt(val))}
            val={values['parallelThreads']}
          />
        </Col>
      </Row>
      <Row>
        <Col lg={8}>
          <Field
            name="kmsConfigUUID"
            component={YBFormSelect}
            label={'KMS Configuration (Optional)'}
            options={kmsConfigList}
          />
        </Col>
      </Row>
    </div>
  );
}

export function RenameKeyspace({
  values,
  setFieldValue
}: {
  values: Record<string, any>;
  setFieldValue: Function;
}) {
  return (
    <div className="rename-keyspace-step">
      <Row>
        <Col lg={6}>
          <SearchInput
            placeHolder="Search keyspace"
            onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
              setFieldValue('searchText', e.target.value);
            }}
          />
        </Col>
      </Row>
      <Row className="help-text">
        <Col lg={12}>Rename keyspace/database in this backup (Optional)</Col>
      </Row>

      <FieldArray
        name="keyspaces"
        render={({ form: { errors } }) =>
          values.backup.responseList.map((keyspace: Keyspace_Table, index: number) =>
            values['searchText'] &&
            keyspace.keyspace &&
            keyspace.keyspace.indexOf(values['searchText']) === -1 ? null : (
              <Row key={index}>
                <Col lg={6} className="keyspaces-input">
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
                    onValueChanged={(val: any) => setFieldValue(`keyspaces[${index}]`, val)}
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
interface RestoreWithoutRenameProps {
  visible: boolean;
  onHide: Function;
  onSubmit: Function;
  onOverride: Function;
}
const RestoreWithoutRenameModal: FC<RestoreWithoutRenameProps> = ({
  visible,
  onHide,
  onSubmit,
  onOverride
}) => {
  return (
    <YBModalForm
      visible={visible}
      title="Restore Without Rename"
      onHide={onHide}
      className="backup-modal"
      onFormSubmit={async (_values: any, { setSubmitting }: { setSubmitting: Function }) => {
        setSubmitting(false);
        onSubmit();
      }}
      submitLabel="Rename Databases/Keyspaces"
    >
      Warning! You are about to restore a backup from source universe to the same universe without
      providing new names for its keyspaces. This will override the existing keyspaces
    </YBModalForm>
  );
};
