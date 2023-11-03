/*
 * Created on Wed Aug 03 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Field, FieldArray, FormikValues } from 'formik';
import _ from 'lodash';
import React, { FC, useState } from 'react';
import { YBControlledTextInput, YBFormInput, YBFormToggle } from '../../../../common/forms/fields';
import { StorageConfigCreationForm, YBReduxFormSelect } from '../common/StorageConfigCreationForm';
import Close from '../../../../universes/images/close.svg';
import { OptionTypeBase } from 'react-select';
import { Col, Row } from 'react-bootstrap';
import { IStorageProviders } from '../IStorageConfigs';
import { YBLabelledMultiEntryInput } from '../../../../common/forms/fields/YBMultiEntryInput';
import { flatten, isEmpty, uniq, uniqBy } from 'lodash';
import { useMutation, useQuery } from 'react-query';
import {
  addCustomerConfig,
  editCustomerConfig,
  fetchBucketsList
} from '../common/StorageConfigApi';
import { useSelector } from 'react-redux';
import { CloudType } from '../../../../../redesign/helpers/dtos';
import * as Yup from 'yup';
import './CreateAWSConfigForm.scss';
import { toast } from 'react-toastify';
import YBInfoTip from '../../../../common/descriptors/YBInfoTip';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';

interface CreateAWSConfigFormProps {
  visible: boolean;
  editInitialValues: Record<string, any>;
  onHide: () => void;
  fetchConfigs: () => void;
}
type configs = {
  region: OptionTypeBase;
  host_base: OptionTypeBase[];
  bucket: OptionTypeBase;
  folder: string;
};

interface InitialValuesTypes {
  AWS_CONFIGURATION_NAME: string;
  IAM_ROLE_ENABLED: boolean;
  AWS_ACCESS_KEY: string;
  AWS_SECRET_KEY: string;
  MULTI_REGION_AWS_ENABLED: boolean;
  multi_regions: configs[];
  default_bucket: string;
  PROXY_SETTINGS?: {
    PROXY_PORT?: string;
    PROXY_HOST?: string;
    PROXY_USERNAME?: string;
    PROXY_PASSWORD?: string;
  };
}

const MUTLI_REGION_DEFAULT_VALUES = {
  bucket: { value: null, label: null },
  host_base: [],
  folder: '',
  region: { value: null, label: null }
};

const S3_PREFIX = 's3://';

const splitLocationIntoBucketAndFolder = (location: string) => {
  const s3_prefix_removed = location.substring(S3_PREFIX.length);
  const s = s3_prefix_removed.split('/');

  if (s.length <= 1) {
    return {
      bucket: s[0],
      folder: ''
    };
  }
  const folder = s.pop();
  return {
    bucket: s.join('/'),
    folder
  };
};

export const CreateAWSConfigForm: FC<CreateAWSConfigFormProps> = ({
  visible,
  editInitialValues,
  onHide,
  fetchConfigs
}) => {
  const [AWSSecretKey, setAWSSecretKey] = useState('');
  const [AWSAccessKey, setAWSAccessKey] = useState('');
  const [iamRoleEnabled, setIAMRoleEnabled] = useState(false);

  const awsProviders = useSelector((state: any) =>
    state.cloud.providers.data
      .filter((p: any) => p.code === CloudType.aws)
      .map((t: any) => t.regions)
  );
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const enableS3BackupProxy =
    featureFlags.test.enableS3BackupProxy || featureFlags.released.enableS3BackupProxy;

  const isEditMode = !isEmpty(editInitialValues);

  const isCredentialsFilled = (AWSSecretKey !== '' && AWSAccessKey !== '') || iamRoleEnabled;

  const { data: resp, isLoading: isBucketListLoading } = useQuery(
    ['awsBucketList', AWSSecretKey, AWSAccessKey, iamRoleEnabled],
    () =>
      fetchBucketsList(IStorageProviders.AWS, {
        AWS_SECRET_ACCESS_KEY: AWSSecretKey,
        AWS_ACCESS_KEY_ID: AWSAccessKey,
        IAM_INSTANCE_PROFILE: iamRoleEnabled.toString()
      }),
    {
      enabled: isCredentialsFilled
    }
  );

  /**
   * Create new storage config
   */
  const doAddStorageConfig = useMutation(
    (values: Record<string, any>) => {
      return addCustomerConfig(values);
    },
    {
      onSuccess: () => {
        toast.success(`Config created successfully!.`);
        onHide();
        fetchConfigs();
      },
      onError: (err: any) => {
        toast.error(createErrorMessage(err));
      }
    }
  );

  /**
   * Update existing storage config
   */
  const doUpdateStorageConfig = useMutation(
    (values: Record<string, any>) => {
      return editCustomerConfig(values);
    },
    {
      onSuccess: () => {
        toast.success(`Config updated successfully!.`);
        onHide();
        fetchConfigs();
      },
      onError: (err: any) => {
        toast.error(createErrorMessage(err));
      }
    }
  );

  const onAWSSecretKeyBlur = (e: any) => {
    const val = e.target.value;
    if (val && val !== AWSSecretKey && !isEditMode) {
      setAWSSecretKey(val);
    }
  };

  const onAWSAccessKeyBlur = (e: any) => {
    const val = e.target.value;
    if (val && val !== AWSAccessKey && !isEditMode) {
      setAWSAccessKey(val);
    }
  };

  if (!visible) {
    return null;
  }

  const initialValues: InitialValuesTypes = {
    AWS_CONFIGURATION_NAME: '',
    IAM_ROLE_ENABLED: false,
    AWS_ACCESS_KEY: '',
    AWS_SECRET_KEY: '',
    MULTI_REGION_AWS_ENABLED: false,
    multi_regions: [MUTLI_REGION_DEFAULT_VALUES],
    default_bucket: '0',
    PROXY_SETTINGS: {
      PROXY_HOST: '',
      PROXY_PORT: '',
      PROXY_PASSWORD: '',
      PROXY_USERNAME: ''
    }
  };

  if (isEditMode) {
    initialValues.AWS_ACCESS_KEY = editInitialValues.data['AWS_ACCESS_KEY_ID'];

    initialValues.AWS_SECRET_KEY = editInitialValues.data['AWS_SECRET_ACCESS_KEY'];
    initialValues.AWS_CONFIGURATION_NAME = editInitialValues.configName;

    if (editInitialValues.data?.PROXY_SETTINGS?.PROXY_HOST)
      initialValues.PROXY_SETTINGS = editInitialValues.data['PROXY_SETTINGS'];

    if (editInitialValues.data.REGION_LOCATIONS?.length > 0) {
      initialValues.MULTI_REGION_AWS_ENABLED = true;

      initialValues.multi_regions = editInitialValues.data.REGION_LOCATIONS.map(
        (r: any, index: number) => {
          const bucketAndFolder = splitLocationIntoBucketAndFolder(r.LOCATION);

          if (
            editInitialValues.data['BACKUP_LOCATION'] === `${S3_PREFIX}${bucketAndFolder.bucket}`
          ) {
            initialValues['default_bucket'] = String(index);
          }

          return {
            region: { value: r.REGION, label: r.REGION },
            bucket: { value: bucketAndFolder.bucket, label: bucketAndFolder.bucket },
            folder: bucketAndFolder.folder,
            host_base: r.AWS_HOST_BASE ? [{ value: r.AWS_HOST_BASE, label: r.AWS_HOST_BASE }] : []
          };
        }
      );
    } else {
      const bucketAndFolder = splitLocationIntoBucketAndFolder(
        editInitialValues.data.BACKUP_LOCATION
      );
      initialValues.multi_regions = [
        {
          host_base: editInitialValues.data.AWS_HOST_BASE
            ? [
                {
                  value: editInitialValues.data.AWS_HOST_BASE,
                  label: editInitialValues.data.AWS_HOST_BASE
                }
              ]
            : [],
          region: [{ value: null, label: null }],
          bucket: { label: bucketAndFolder.bucket, value: bucketAndFolder.bucket },
          folder: bucketAndFolder.folder ?? ''
        }
      ];
    }
  }

  let buckets: OptionTypeBase[] = [];
  let hostBase: OptionTypeBase[] = [];

  if (resp?.data) {
    buckets = uniq(Object.keys(resp.data)).map((e) => {
      return { value: e, label: e };
    });
    hostBase = uniq(Object.values(resp.data)).map((e) => {
      return { value: e, label: e };
    });
  }

  //grab all distinct regions from gcp provider
  const regionsInAws: OptionTypeBase[] = uniqBy(
    flatten(awsProviders)?.map((r: any) => {
      return { value: r.code, label: r.code };
    }) ?? [],
    'label'
  );

  const onSubmit = (values: InitialValuesTypes | FormikValues, _: any) => {
    const payload = {
      type: 'STORAGE',
      name: IStorageProviders.AWS.toUpperCase(),
      configName: values.AWS_CONFIGURATION_NAME,
      data: {
        IAM_INSTANCE_PROFILE: values.IAM_ROLE_ENABLED.toString(),
        AWS_ACCESS_KEY_ID: values.AWS_ACCESS_KEY,
        AWS_SECRET_ACCESS_KEY: values.AWS_SECRET_KEY
      }
    };

    if (!values.MULTI_REGION_AWS_ENABLED) {
      payload.data['BACKUP_LOCATION'] = `${S3_PREFIX}${values.multi_regions[0].bucket}/${
        values.multi_regions[0].folder ?? ''
      }`;
      if (values.multi_regions[0]?.host_base?.value) {
        payload.data['AWS_HOST_BASE'] = values.multi_regions[0]?.host_base?.value;
      }
    } else {
      payload['data']['REGION_LOCATIONS'] = values['multi_regions'].map((r: configs) => {
        const regionValues = {
          REGION: r.region.value,
          LOCATION: `${S3_PREFIX}${r.bucket.value}/${r.folder ?? ''}`
        };
        if (r.host_base?.[0]?.value) {
          regionValues['AWS_HOST_BASE'] = r.host_base[0].value;
        }
        return regionValues;
      });
    }
    const default_bucket_index = values.MULTI_REGION_AWS_ENABLED
      ? parseInt(values['default_bucket'])
      : 0;

    payload['data']['BACKUP_LOCATION'] = `${S3_PREFIX}${
      values['multi_regions'][default_bucket_index]?.bucket?.value
    }/${values['multi_regions'][default_bucket_index].folder ?? ''}`;

    if (values?.PROXY_SETTINGS?.PROXY_HOST) {
      payload['data']['PROXY_SETTINGS'] = {};
      payload['data']['PROXY_SETTINGS']['PROXY_HOST'] = values.PROXY_SETTINGS.PROXY_HOST;

      if (values?.PROXY_SETTINGS?.PROXY_PORT)
        payload['data']['PROXY_SETTINGS']['PROXY_PORT'] = values.PROXY_SETTINGS.PROXY_PORT;
      if (values?.PROXY_SETTINGS?.PROXY_USERNAME) {
        payload['data']['PROXY_SETTINGS']['PROXY_USERNAME'] = values.PROXY_SETTINGS.PROXY_USERNAME;
        if (values?.PROXY_SETTINGS?.PROXY_PASSWORD)
          payload['data']['PROXY_SETTINGS']['PROXY_PASSWORD'] =
            values.PROXY_SETTINGS.PROXY_PASSWORD;
      }
    }

    if (isEditMode) {
      doUpdateStorageConfig.mutate({ configUUID: editInitialValues['configUUID'], ...payload });
    } else {
      doAddStorageConfig.mutate(payload);
    }
  };

  const validationSchema = Yup.object().shape({
    AWS_CONFIGURATION_NAME: Yup.string().required('Configuration name is required'),
    AWS_ACCESS_KEY: Yup.string().required('Access Key is required'),
    AWS_SECRET_KEY: Yup.string().required('Secret Key is required'),
    multi_regions: Yup.array()
      .when('MULTI_REGION_AWS_ENABLED', {
        is: (enabled) => enabled,
        then: Yup.array(
          Yup.object().shape({
            region: Yup.object().shape({
              value: Yup.string().required('Region is required').typeError('Region is required')
            }),
            bucket: Yup.object()
              .shape({
                value: Yup.string().required('Bucket is required').typeError('Bucket is required')
              })
              .typeError('Bucket is required'),
            folder: Yup.string()
          })
        )
          .min(1, 'Atleast one region has to be configured')
          .test('unique_regions', 'Regions should be unique', (value: any) => {
            const regions = flatten(
              value.map((e: configs) => e.region.value).filter((e: string) => e !== null)
            );
            return regions.length === uniq(regions).length;
          })
      })
      .when('MULTI_REGION_AWS_ENABLED', {
        is: (enabled) => !enabled,
        then: Yup.array(
          Yup.object().shape({
            bucket: Yup.object()
              .shape({
                value: Yup.string().required('Bucket is required').nullable()
              })
              .typeError('Bucket is required'),
            folder: Yup.string()
          })
        )
      }),
    PROXY_SETTINGS: Yup.object().shape({
      PROXY_PORT: Yup.string().when('PROXY_HOST', {
        is: (value: string) => !_.isEmpty(value),
        then: Yup.string().required('Port is a required')
      }),
      PROXY_PASSWORD: Yup.string().when('PROXY_USERNAME', {
        is: (value: string) => !_.isEmpty(value),
        then: Yup.string().required('Password is a required')
      })
    })
  });

  return (
    <StorageConfigCreationForm
      onCancel={onHide}
      initialValues={initialValues}
      validationSchema={validationSchema}
      type="CREATE"
      onSubmit={onSubmit}
      components={({ setFieldValue, values, errors }) => {
        const updateFieldValue = (fieldName: keyof InitialValuesTypes, val: any) => {
          setFieldValue(fieldName, val);
        };

        return (
          <Row className="aws-storage-config-form">
            <Col lg={8}>
              <Row className="config-provider-row">
                <Col lg={2} className="form-item-custom-label">
                  <div>Configuration Name</div>
                </Col>
                <Col lg={9}>
                  <Field
                    name="AWS_CONFIGURATION_NAME"
                    placeholder="Configuration Name"
                    component={YBFormInput}
                    onValueChanged={(value: string) =>
                      setFieldValue('AWS_CONFIGURATION_NAME', value)
                    }
                  />
                </Col>
              </Row>
              <Row className="config-provider-row">
                <Col lg={2} className="form-item-custom-label">
                  <div>IAM Role</div>
                </Col>
                <Col lg={9}>
                  <Field
                    name="IAM_ROLE_ENABLED"
                    component={YBFormToggle}
                    subLabel="Whether to use instance's IAM role for S3 backup."
                    onChange={(_: any, e: React.ChangeEvent<HTMLInputElement>) =>
                      setIAMRoleEnabled(e.target.checked)
                    }
                  />
                </Col>
              </Row>
              <Row className="config-provider-row">
                <Col lg={2} className="form-item-custom-label">
                  <div>Access Key</div>
                </Col>
                <Col lg={9}>
                  <Field
                    name="AWS_ACCESS_KEY"
                    placeholder="AWS Access Key"
                    component={YBFormInput}
                    onValueChanged={(value: string) => setFieldValue('AWS_ACCESS_KEY', value)}
                    disabled={values['IAM_ROLE_ENABLED']}
                    onBlur={onAWSAccessKeyBlur}
                  />
                </Col>
              </Row>
              <Row className="config-provider-row">
                <Col lg={2} className="form-item-custom-label">
                  <div>Access Secret</div>
                </Col>
                <Col lg={9}>
                  <Field
                    name="AWS_SECRET_KEY"
                    placeholder="AWS Secret Key"
                    component={YBFormInput}
                    onValueChanged={(value: string) => setFieldValue('AWS_SECRET_KEY', value)}
                    disabled={values['IAM_ROLE_ENABLED']}
                    onBlur={onAWSSecretKeyBlur}
                  />
                </Col>
              </Row>
              <div className="form-divider" />
              <Row className="config-provider-row">
                <Col lg={2} className="form-item-custom-label">
                  <div>Multi Region Support</div>
                </Col>
                <Col lg={9}>
                  <Field
                    name="MULTI_REGION_AWS_ENABLED"
                    component={YBFormToggle}
                    isReadOnly={isEditMode}
                    subLabel="Specify a bucket for each region"
                  />
                </Col>
              </Row>
              {!values.MULTI_REGION_AWS_ENABLED && (
                <>
                  <Row className="config-provider-row">
                    <Col lg={2} className="form-item-custom-label">
                      <div>S3 Bucket Host Base</div>
                    </Col>
                    <Col lg={9}>
                      {getHostBaseField(
                        0,
                        values.multi_regions[0]?.host_base,
                        hostBase,
                        updateFieldValue,
                        isEditMode,
                        false
                      )}
                    </Col>
                    <Col lg={1} className="config-zone-tooltip">
                      <YBInfoTip
                        title="S3 Host"
                        content="Host of S3 bucket. Defaults to s3.amazonaws.com"
                      />
                    </Col>
                  </Row>
                  <Row className="config-provider-row multi-region-aws-disabled-config">
                    <Col lg={2} className="form-item-custom-label">
                      <div>Storage bucket</div>
                    </Col>
                    <Col lg={9} className="bucket-controls">
                      <div>
                        {getBucket(
                          0,
                          values.multi_regions[0]?.bucket,
                          updateFieldValue,
                          buckets,
                          isEditMode || !isCredentialsFilled,
                          isBucketListLoading
                        )}
                        {/* <span className="field-error">{errors?.multi_regions?.[0].bucket?.value}</span> */}
                      </div>
                      <div className="divider lean" />
                      {getFolder(
                        0,
                        values.multi_regions[0]?.folder,
                        false,
                        () => {},
                        updateFieldValue,
                        isEditMode || !isCredentialsFilled
                      )}
                    </Col>
                  </Row>
                </>
              )}
              {values.MULTI_REGION_AWS_ENABLED && (
                <div className="multi-region-enabled aws with-border">
                  <FieldArray
                    name="multi_regions"
                    render={(arrayHelper) => (
                      <>
                        {values.multi_regions.map((region: configs, index: number) => (
                          <Row key={index} className="config-provider-row">
                            <Col lg={12} md={12} className="no-left-padding">
                              {MultiRegionControls(
                                region,
                                index,
                                regionsInAws,
                                buckets,
                                hostBase,
                                () => {
                                  arrayHelper.remove(index);
                                },
                                updateFieldValue,
                                isEditMode || !isCredentialsFilled,
                                values['default_bucket'] === String(index),
                                errors?.multi_regions?.[index],
                                isBucketListLoading
                              )}
                            </Col>
                          </Row>
                        ))}
                        {!isEditMode && (
                          <a
                            href="#!"
                            className="on-prem-add-link add-region-link"
                            onClick={(e) => {
                              e.preventDefault();
                              arrayHelper.push({
                                ...MUTLI_REGION_DEFAULT_VALUES
                              });
                            }}
                          >
                            <i className="fa fa-plus-circle" />
                            Add Region
                          </a>
                        )}
                      </>
                    )}
                  />
                </div>
              )}
              {typeof errors.multi_regions === 'string' && (
                <span className="field-error">{errors.multi_regions}</span>
              )}
              {enableS3BackupProxy && (
                <>
                  <Row className="config-provider-row backup-proxy-config">
                    <h4>Proxy Configuration</h4>
                  </Row>
                  <div className="divider"></div>

                  <Row className="config-provider-row backup-proxy-config">
                    <Col lg={2} className="form-item-custom-label">
                      Host
                    </Col>
                    <Col lg={9}>
                      <Field
                        name="PROXY_SETTINGS.PROXY_HOST"
                        placeholder="Proxy Host"
                        component={YBFormInput}
                        onValueChanged={(value: string) =>
                          setFieldValue('PROXY_SETTINGS.PROXY_HOST', value)
                        }
                      />
                    </Col>
                    <Col lg={1} className="config-zone-tooltip">
                      <YBInfoTip title="Host" content="Host address of the proxy server" />
                    </Col>
                  </Row>

                  <Row className="config-provider-row">
                    <Col lg={2} className="form-item-custom-label">
                      Port
                    </Col>
                    <Col lg={9}>
                      <Field
                        name="PROXY_SETTINGS.PROXY_PORT"
                        placeholder="Proxy Port"
                        component={YBFormInput}
                        type="number"
                        onValueChanged={(value: string) =>
                          setFieldValue('PROXY_SETTINGS.PROXY_PORT', value)
                        }
                      />
                    </Col>
                    <Col lg={1} className="config-zone-tooltip">
                      <YBInfoTip
                        title="Port"
                        content="Port number at which the proxy server is running"
                      />
                    </Col>
                  </Row>

                  <Row className="config-provider-row">
                    <Col lg={2} className="form-item-custom-label">
                      Username (Optional)
                    </Col>
                    <Col lg={9}>
                      <Field
                        name="PROXY_SETTINGS.PROXY_USERNAME"
                        placeholder="Proxy Username"
                        component={YBFormInput}
                        onValueChanged={(value: string) =>
                          setFieldValue('PROXY_SETTINGS.PROXY_USERNAME', value)
                        }
                      />
                    </Col>
                    <Col lg={1} className="config-zone-tooltip">
                      <YBInfoTip title="Username" content="Username for authentication" />
                    </Col>
                  </Row>

                  <Row className="config-provider-row">
                    <Col lg={2} className="form-item-custom-label">
                      Password (Optional)
                    </Col>
                    <Col lg={9}>
                      <Field
                        name="PROXY_SETTINGS.PROXY_PASSWORD"
                        placeHolder="Proxy Password"
                        component={YBFormInput}
                        type="password"
                        onValueChanged={(value: string) =>
                          setFieldValue('PROXY_SETTINGS.PROXY_PASSWORD', value)
                        }
                        autocomplete="new-password"
                      />
                    </Col>
                    <Col lg={1} className="config-zone-tooltip">
                      <YBInfoTip title="Password" content="Password for authentication" />
                    </Col>
                  </Row>
                </>
              )}
            </Col>
          </Row>
        );
      }}
    />
  );
};

const MultiRegionControls = (
  field: configs,
  index: number,
  regionsInAws: OptionTypeBase[],
  bucketsList: OptionTypeBase[],
  hostBaseList: OptionTypeBase[],
  onremoveField: Function,
  updateFieldVal: Function,
  isDisabled = false,
  isDefaultBucket = false,
  // values:
  errors: Record<string, any> | undefined,
  isBucketListLoading = false
) => (
  <div className="multi-region-enabled-fields">
    <div>
      <Field
        name={`multi_regions.${index}.region`}
        component={YBReduxFormSelect}
        options={regionsInAws}
        label="Region"
        additionalProps={{
          className: 'aws-field-region',
          placeholder: 'Select Region'
        }}
        isDisabled={isDisabled}
        value={field?.region}
        onChange={(val: any) => {
          updateFieldVal(`multi_regions.${index}.region`, val);
        }}
      />
      {/* <span className="field-error">{errors?.region?.value}</span> */}
    </div>

    {getHostBaseField(index, field?.host_base, hostBaseList, updateFieldVal, isDisabled, true)}
    <div className="divider" />
    <div>
      {getBucket(
        index,
        field?.bucket,
        updateFieldVal,
        bucketsList,
        isDisabled,
        isBucketListLoading
      )}
    </div>
    <div className="divider lean" />
    <div className="folder">
      <div>{getFolder(index, field?.folder, true, onremoveField, updateFieldVal, isDisabled)}</div>
      <div className="is-default-bucket">
        <Field
          name="default_bucket"
          component="input"
          type="radio"
          value={index}
          checked={isDefaultBucket}
          label="Set as default bucket"
          disabled={isDisabled}
        />
        Set as default bucket
      </div>
    </div>
  </div>
);

const getBucket = (
  index: number,
  value: OptionTypeBase | undefined,
  updateFieldVal: Function,
  bucketList: OptionTypeBase[],
  isDisabled: boolean,
  isBucketListLoading: boolean
) => (
  <Field
    name={`multi_regions.${index}.bucket`}
    component={YBLabelledMultiEntryInput}
    additionalProps={{
      className: 'field-bucket',
      isMulti: false
    }}
    className="field-bucket"
    defaultOptions={bucketList}
    label="Bucket"
    placeholder="Select Bucket"
    onChange={(val: OptionTypeBase) => {
      updateFieldVal(`multi_regions.${index}.bucket`, val);
    }}
    isLoading={isBucketListLoading}
    isDisabled={isDisabled}
    val={value}
  />
);
const getHostBaseField = (
  index: number,
  value: OptionTypeBase | undefined,
  hostBases: OptionTypeBase[],
  updateFieldVal: Function,
  isDisabled: boolean,
  showLabel = true
) => (
  <>
    <Field
      name={`multi_regions.${index}.host_base`}
      component={YBLabelledMultiEntryInput}
      defaultOptions={hostBases}
      label={showLabel ? 'Host Base' : ''}
      placeholder="Select host base"
      className="field-host-base"
      onChange={(val: any) => {
        updateFieldVal(`multi_regions.${index}.host_base`, val);
      }}
      val={value}
      isDisabled={isDisabled}
    />
  </>
);

const getFolder = (
  index: number,
  value: string,
  showRemoveIcon: boolean,
  onremoveField: Function,
  updateFieldVal: Function,
  isDisabled: boolean
) => (
  <div className="aws-folder-field">
    <Field
      name={`multi_regions.${index}.folder`}
      component={YBControlledTextInput}
      label="Folder (optional)"
      onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
        updateFieldVal(`multi_regions.${index}.folder`, e.target.value);
      }}
      val={value}
      isReadOnly={isDisabled}
    />
    {showRemoveIcon && (
      <img
        alt="Remove"
        className="remove-field-icon"
        src={Close}
        width="22"
        onClick={() => onremoveField()}
      />
    )}
  </div>
);
