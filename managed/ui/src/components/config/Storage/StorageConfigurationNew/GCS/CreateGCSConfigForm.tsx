/*
 * Created on Thu Jul 28 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Field, FieldArray, FormikValues } from 'formik';
import React, { FC, useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import { useSelector } from 'react-redux';
import * as Yup from 'yup';
import { YBControlledTextInput, YBFormInput, YBFormToggle } from '../../../../common/forms/fields';
import { StorageConfigCreationForm, YBReduxFormSelect } from '../common/StorageConfigCreationForm';
import { OptionTypeBase } from 'react-select';
import { flatten, isEmpty, uniq, uniqBy } from 'lodash';
import { useMutation, useQuery } from 'react-query';
import {
  addCustomerConfig,
  editCustomerConfig,
  fetchBucketsList
} from '../common/StorageConfigApi';
import { IStorageProviders } from '../IStorageConfigs';
import { CloudType } from '../../../../../redesign/helpers/dtos';
import { toast } from 'react-toastify';
import './CreateGCSConfigForm.scss';
import Close from '../../../../universes/images/close.svg';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';

interface CreateGCSConfigFormProps {
  visible: boolean;
  editInitialValues: Record<string, any>;
  onHide: () => void;
  fetchConfigs: () => void;
}

type configs = {
  region: OptionTypeBase;
  bucket: OptionTypeBase;
  folder: string;
};

interface InitialValuesTypes {
  GCS_CONFIGURATION_NAME: string;
  GCS_CREDENTIALS_JSON: string;
  multi_regions: configs[];
  MULTI_REGION_GCP_ENABLED: boolean;
  default_bucket: string;
}

export const GCS_PREFIX = 'gs://';

const MUTLI_REGION_DEFAULT_VALUES = {
  bucket: { value: null, label: null },
  folder: '',
  region: { value: null, label: null }
};

/**
 *  splits the location into buckets and folder
 *  gs://foo/bar => bucket:foo, folder:bar
 */
const convertLocationToBucketAndFolder = (location: string) => {
  const gcs_prefix_removed = location.substring(GCS_PREFIX.length);
  const s = gcs_prefix_removed.split('/');
  return {
    bucket: s[0],
    folder: s[1] ?? ''
  };
};

export const CreateGCSConfigForm: FC<CreateGCSConfigFormProps> = ({
  visible,
  editInitialValues,
  onHide,
  fetchConfigs
}) => {
  const [GCSCredentials, setGCSCredentials] = useState('');

  const isEditMode = !isEmpty(editInitialValues);

  const gcpProviders = useSelector((state: any) =>
    state.cloud.providers.data
      .filter((p: any) => p.code === CloudType.gcp)
      .map((t: any) => t.regions)
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

  const { data: resp, isLoading: isBucketListLoading } = useQuery(
    ['gcsBucketList', GCSCredentials],
    () =>
      fetchBucketsList(IStorageProviders.GCS, {
        GCS_CREDENTIALS_JSON: GCSCredentials
      }),
    {
      enabled: GCSCredentials !== ''
    }
  );

  if (!visible) {
    return null;
  }

  const buckets =
    resp?.data?.map((t: string) => {
      return { value: t, label: t };
    }) ?? [];

  //grab all distinct regions from gcp provider
  const regionsInGcp: OptionTypeBase[] = uniqBy(
    flatten(gcpProviders)?.map((r: any) => {
      return { value: r.code, label: r.code };
    }) ?? [],
    'label'
  );

  const onSubmit = (values: InitialValuesTypes | FormikValues, _: any) => {
    const payload = {
      type: 'STORAGE',
      name: IStorageProviders.GCS.toUpperCase(),
      configName: values['GCS_CONFIGURATION_NAME'],
      data: {
        GCS_CREDENTIALS_JSON: values['GCS_CREDENTIALS_JSON']
      }
    };

    if (values['MULTI_REGION_GCP_ENABLED']) {
      payload['data']['REGION_LOCATIONS'] = values['multi_regions'].map((r: configs) => {
        return {
          REGION: r.region.value,
          LOCATION: `${GCS_PREFIX}${r.bucket.value}/${r.folder}`
        };
      });
    }

    // if multi_region is enabled, then get the option selected , else default to the first one.
    const default_bucket_index = values['MULTI_REGION_GCP_ENABLED']
      ? parseInt(values['default_bucket'])
      : 0;

    payload['data'][
      'BACKUP_LOCATION'
    ] = `${GCS_PREFIX}${values['multi_regions'][default_bucket_index].bucket.value}/${values['multi_regions'][default_bucket_index].folder}`;

    if (isEditMode) {
      doUpdateStorageConfig.mutate({ configUUID: editInitialValues['configUUID'], ...payload });
    } else {
      doAddStorageConfig.mutate(payload);
    }
  };

  // after user enter the gcs credentials, then try to fetch the list of buckets configured.
  const onGCSCredentialsBlur = (e: any) => {
    const val = e.target.value;
    if (val && val !== GCSCredentials && !isEditMode) {
      setGCSCredentials(val);
    }
  };

  const initialValues: InitialValuesTypes = {
    GCS_CONFIGURATION_NAME: '',
    GCS_CREDENTIALS_JSON: '',
    MULTI_REGION_GCP_ENABLED: false,
    multi_regions: [MUTLI_REGION_DEFAULT_VALUES],
    default_bucket: '0'
  };

  // in Edit mode , convert api values to form values
  if (isEditMode) {
    initialValues['GCS_CONFIGURATION_NAME'] = editInitialValues['configName'];
    initialValues['GCS_CREDENTIALS_JSON'] = editInitialValues.data['GCS_CREDENTIALS_JSON'];

    initialValues['MULTI_REGION_GCP_ENABLED'] =
      editInitialValues.data['REGION_LOCATIONS']?.length > 0;

    if (initialValues['MULTI_REGION_GCP_ENABLED']) {
      initialValues['multi_regions'] = editInitialValues.data['REGION_LOCATIONS'].map(
        (r: any, i: number) => {
          const bucketAndFolder = convertLocationToBucketAndFolder(r.LOCATION);

          if (r.LOCATION === editInitialValues.data['BACKUP_LOCATION']) {
            initialValues['default_bucket'] = i + '';
          }

          return {
            region: { value: r.REGION, label: r.REGION },
            bucket: { value: bucketAndFolder.bucket, label: bucketAndFolder.bucket },
            folder: bucketAndFolder.folder
          };
        }
      );
    } else {
      const bucketAndFolder = convertLocationToBucketAndFolder(
        editInitialValues.data['BACKUP_LOCATION']
      );
      initialValues['multi_regions'] = [
        {
          bucket: { value: bucketAndFolder.bucket, label: bucketAndFolder.bucket },
          folder: bucketAndFolder.folder,
          region: { value: null, region: null }
        }
      ];
    }
  }

  const validationSchema = Yup.object().shape({
    GCS_CONFIGURATION_NAME: Yup.string().required('Configuration name is required'),
    GCS_CREDENTIALS_JSON: Yup.string().required('Credentials JSON is required'),
    multi_regions: Yup.array()
      .when('MULTI_REGION_GCP_ENABLED', {
        is: (enabled) => enabled,
        then: Yup.array()
          .of(
            Yup.object().shape({
              region: Yup.object().shape({
                value: Yup.string().required().typeError('Region is required')
              }),
              bucket: Yup.object().shape({
                value: Yup.string().required().typeError('Bucket is required')
              }),
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
      .when('MULTI_REGION_GCP_ENABLED', {
        is: (enabled) => !enabled,
        then: Yup.array().of(
          Yup.object().shape({
            bucket: Yup.object().shape({
              value: Yup.string().required().typeError('Bucket is required')
            }),
            folder: Yup.string()
          })
        )
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
        return (
          <Row>
            <Col lg={8}>
              <Row>
                <Col lg={2} className="form-item-custom-label">
                  <div>Configuration Name</div>
                </Col>
                <Col lg={9}>
                  <Field
                    name="GCS_CONFIGURATION_NAME"
                    placeHolder="Configuration Name"
                    component={YBFormInput}
                    onValueChanged={(value: string) =>
                      setFieldValue('GCS_CONFIGURATION_NAME', value)
                    }
                  />
                </Col>
              </Row>
              <Row>
                <Col lg={2} className="form-item-custom-label">
                  <div>GCS Credentials</div>
                </Col>
                <Col lg={9}>
                  <Field
                    name="GCS_CREDENTIALS_JSON"
                    placeHolder="GCS Credentials JSON"
                    component={YBFormInput}
                    onValueChanged={(value: string) => setFieldValue('GCS_CREDENTIALS_JSON', value)}
                    onBlur={onGCSCredentialsBlur}
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
                    name="MULTI_REGION_GCP_ENABLED"
                    component={YBFormToggle}
                    isReadOnly={isEditMode}
                    subLabel="Specify a bucket for each region"
                  />
                </Col>
              </Row>
              {!values.MULTI_REGION_GCP_ENABLED && (
                <Row className="config-provider-row">
                  <Col lg={2} className="gcs-bucket-label form-item-custom-label">
                    <div>GCS Bucket</div>
                  </Col>
                  <Col lg={9} className="multi-region-enabled">
                    <div className="multi-region-enabled-fields">
                      {getGCPBucketAndFolder(
                        values.multi_regions[0],
                        0,
                        buckets,
                        () => {},
                        (fieldName: string, val: any) => {
                          setFieldValue(fieldName, val);
                        },
                        false,
                        false,
                        isEditMode || GCSCredentials === '',
                        false,
                        isBucketListLoading,
                        errors?.multi_regions?.[0]
                      )}
                    </div>
                  </Col>
                </Row>
              )}
              {values.MULTI_REGION_GCP_ENABLED && (
                <div className="multi-region-enabled with-border">
                  <FieldArray
                    name="multi_regions"
                    render={(arrayHelper) => (
                      <>
                        {values.multi_regions.map((region: configs, index: number) => (
                          // eslint-disable-next-line react/no-array-index-key
                          <Row key={index} className="padding-bottom">
                            <Col lg={12} md={12} className="no-left-padding">
                              {MultiRegionControls(
                                region,
                                index,
                                buckets,
                                regionsInGcp,
                                () => {
                                  arrayHelper.remove(index);
                                },
                                (fieldName: string, val: any) => {
                                  setFieldValue(fieldName, val);
                                },
                                isEditMode || GCSCredentials === '',
                                values['default_bucket'] === String(index),
                                isBucketListLoading,
                                errors?.multi_regions?.[index]
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
  buckets: OptionTypeBase[],
  regionsInGcp: OptionTypeBase[],
  onremoveField: Function,
  updateFieldVal: Function,
  isDisabled = false,
  isdefaultBucket = false,
  isBucketListLoading: boolean,
  errors: Record<string, any> | undefined
) => (
  <div className="multi-region-enabled-fields">
    <div>
      <Field
        name={`multi_regions.${index}.region`}
        component={YBReduxFormSelect}
        options={regionsInGcp}
        label="Region"
        width="280px"
        className="field-region"
        placeholder="Select region"
        value={field?.region ?? { value: null, label: null }}
        onChange={(val: any) => {
          updateFieldVal(`multi_regions.${index}.region`, val);
        }}
        isDisabled={isDisabled}
      />
      <span className="field-error">{errors?.region?.value}</span>
    </div>
    {getGCPBucketAndFolder(
      field,
      index,
      buckets,
      onremoveField,
      updateFieldVal,
      true,
      true,
      isDisabled,
      isdefaultBucket,
      isBucketListLoading,
      errors
    )}
  </div>
);

const getGCPBucketAndFolder = (
  field: configs,
  index: number,
  bucketsList: OptionTypeBase[],
  onremoveField: Function,
  updateFieldVal: Function,
  showCloseIcon = false,
  showDefaultRegionOption = false,
  isDisabled = false,
  isDefaultBucket = false,
  isBucketListLoading: boolean,
  errors: Record<string, any> | undefined
) => (
  <>
    <div>
      <Field
        name={`multi_regions.${index}.bucket`}
        component={YBReduxFormSelect}
        options={bucketsList}
        label="Bucket"
        className="field-bucket"
        placeholder="Select bucket"
        value={field?.bucket ?? ''}
        onChange={(val: any) => {
          updateFieldVal(`multi_regions.${index}.bucket`, val);
        }}
        isDisabled={isDisabled}
        isLoading={isBucketListLoading}
      />
      <span className="field-error">{errors?.bucket?.value}</span>
    </div>
    <div className="divider lean" />
    <div className="folder">
      <div className="display-flex">
        <Field
          name={`multi_regions.${index}.folder`}
          component={YBControlledTextInput}
          label="Folder (optional)"
          placeHolder="Folder name"
          className="field-bucket"
          onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
            updateFieldVal(`multi_regions.${index}.folder`, e.target.value);
          }}
          insetError="Error"
          val={field?.folder ?? ''}
          isReadOnly={isDisabled}
        />
        {showCloseIcon && (
          <img
            alt="Remove"
            className="remove-field-icon"
            src={Close}
            width="22"
            onClick={() => !isDisabled && onremoveField()}
          />
        )}
      </div>
      {showDefaultRegionOption && (
        <div className="is-default-bucket">
          <Field
            name="default_bucket"
            component="input"
            type="radio"
            label="Set as default bucket"
            value={index}
            disabled={isDisabled}
            checked={isDefaultBucket}
          />
          Set as default bucket
        </div>
      )}
    </div>
  </>
);
