/*
 * Created on Mon Aug 22 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Field, FieldArray, FormikValues } from 'formik';
import React, { FC } from 'react';
import { Col, Row } from 'react-bootstrap';
import { useSelector } from 'react-redux';
import * as Yup from 'yup';
import { YBControlledTextInput, YBFormInput, YBFormToggle } from '../../../../common/forms/fields';
import { StorageConfigCreationForm, YBReduxFormSelect } from '../common/StorageConfigCreationForm';
import { OptionTypeBase } from 'react-select';
import { flatten, isEmpty, uniq, uniqBy } from 'lodash';
import { useMutation } from 'react-query';
import {
  addCustomerConfig,
  editCustomerConfig,
} from '../common/StorageConfigApi';
import { IStorageProviders } from '../IStorageConfigs';
import { CloudType } from '../../../../../redesign/helpers/dtos';
import { toast } from 'react-toastify';
import './CreateAzureConfigForm.scss';
import Close from '../../../../universes/images/close.svg';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';

interface CreateAzureConfigFormProps {
  visible: boolean;
  editInitialValues: Record<string, any>;
  onHide: () => void;
  fetchConfigs: () => void;
}

type configs = {
  region: OptionTypeBase;
  sas_token: string;
  container: string;
  folder: string;
};

interface InitialValuesTypes {
  AZ_CONFIGURATION_NAME: string;
  multi_regions: configs[];
  MULTI_REGION_AZ_ENABLED: boolean;
  default_bucket: string;
}

const MUTLI_REGION_DEFAULT_VALUES: configs = {
  sas_token: '',
  folder: '',
  container: '',
  region: { value: null, label: null }
};

const convertLocationToContainerAndFolder = (location: string) => {
  const url = location.split('//');
  const protocol = url[0];

  const s = url[1].split('/');
  return {
    container: `${protocol}//${s[0]}`,
    folder: s[1] ?? ''
  };
};

export const CreateAzureConfigForm: FC<CreateAzureConfigFormProps> = ({
  visible,
  editInitialValues,
  onHide,
  fetchConfigs
}) => {
  const isEditMode = !isEmpty(editInitialValues);

  const azureProviders = useSelector((state: any) =>
    state.cloud.providers.data
      .filter((p: any) => p.code === CloudType.azu)
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

  if (!visible) {
    return null;
  }

  //grab all distinct regions from gcp provider
  const regionsInAzure: OptionTypeBase[] = uniqBy(
    flatten(azureProviders)?.map((r: any) => {
      return { value: r.code, label: r.code };
    }) ?? [],
    'label'
  );

  const onSubmit = (values: InitialValuesTypes | FormikValues, _: any) => {
    const payload = {
      type: 'STORAGE',
      name: IStorageProviders.AZURE.toUpperCase(),
      configName: values.AZ_CONFIGURATION_NAME,
      data: {
        BACKUP_LOCATION: `${values.multi_regions[0].container}/${
          values.multi_regions[0].folder ?? ''
        }`,
        AZURE_STORAGE_SAS_TOKEN: values.multi_regions[0].sas_token
      }
    };

    if (values.MULTI_REGION_AZ_ENABLED) {
      payload['data']['REGION_LOCATIONS'] = values['multi_regions'].map((r: configs) => {
        return {
          REGION: r.region.value,
          LOCATION: `${r.container}/${r.folder}`,
          AZURE_STORAGE_SAS_TOKEN: r.sas_token
        };
      });
    }

    const default_bucket_index = values.MULTI_REGION_AZ_ENABLED
      ? parseInt(values['default_bucket'])
      : 0;

    payload['data'][
      'BACKUP_LOCATION'
    ] = `${values['multi_regions'][default_bucket_index].container}/${values['multi_regions'][default_bucket_index].folder}`;

    if (isEditMode) {
      doUpdateStorageConfig.mutate({ configUUID: editInitialValues['configUUID'], ...payload });
    } else {
      doAddStorageConfig.mutate(payload);
    }
  };

  const initialValues: InitialValuesTypes = {
    AZ_CONFIGURATION_NAME: '',
    MULTI_REGION_AZ_ENABLED: false,
    multi_regions: [MUTLI_REGION_DEFAULT_VALUES],
    default_bucket: '0'
  };

  // in Edit mode , convert api values to form values
  if (isEditMode) {
    initialValues.AZ_CONFIGURATION_NAME = editInitialValues['configName'];

    initialValues.MULTI_REGION_AZ_ENABLED = editInitialValues.data['REGION_LOCATIONS']?.length > 0;

    if (initialValues.MULTI_REGION_AZ_ENABLED) {
      initialValues['multi_regions'] = editInitialValues.data['REGION_LOCATIONS'].map(
        (r: any, i: number) => {
          const containerAndFolder = convertLocationToContainerAndFolder(r.LOCATION);

          if (r.LOCATION === editInitialValues.data['BACKUP_LOCATION']) {
            initialValues['default_bucket'] = i + '';
          }

          return {
            region: { value: r.REGION, label: r.REGION },
            containet: containerAndFolder.container,
            folder: containerAndFolder.folder,
            sas_token: r.AZURE_STORAGE_SAS_TOKEN
          };
        }
      );
    } else {
      const containerAndFolder = convertLocationToContainerAndFolder(
        editInitialValues.data['BACKUP_LOCATION']
      );
      initialValues['multi_regions'] = [
        {
          container: containerAndFolder.container,
          folder: containerAndFolder.folder,
          region: { value: null, region: null },
          sas_token: editInitialValues.data['AZURE_STORAGE_SAS_TOKEN']
        }
      ];
    }
  }

  const validationSchema = Yup.object().shape({
    AZ_CONFIGURATION_NAME: Yup.string().required('Configuration name is required'),
    multi_regions: Yup.array()
      .when('MULTI_REGION_AZ_ENABLED', {
        is: (enabled) => enabled,
        then: Yup.array()
          .of(
            Yup.object().shape({
              region: Yup.object().shape({
                value: Yup.string().required().typeError('Region is required')
              }),
              sas_token: Yup.string().required('SAS token is required'),
              container: Yup.string().required('Container is required'),
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
      .when('MULTI_REGION_AZ_ENABLED', {
        is: (enabled) => !enabled,
        then: Yup.array().of(
          Yup.object().shape({
            sas_token: Yup.string().required('SAS token is required'),
            container: Yup.string().required('Container is required'),
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
      components={({ setFieldValue, values, errors, touched }) => {
        return (
          <Row>
            <Col lg={8}>
              <Row>
                <Col lg={2} className="form-item-custom-label">
                  <div>Configuration Name</div>
                </Col>
                <Col lg={9}>
                  <Field
                    name="AZ_CONFIGURATION_NAME"
                    placeHolder="Configuration Name"
                    component={YBFormInput}
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
                    name="MULTI_REGION_AZ_ENABLED"
                    component={YBFormToggle}
                    isReadOnly={isEditMode}
                    subLabel="Specify a bucket for each region"
                  />
                </Col>
              </Row>
              {!values.MULTI_REGION_AZ_ENABLED && (
                <>
                  <Row className="config-provider-row">
                    <Col lg={2} className="form-item-custom-label">
                      <div>SAS Token</div>
                    </Col>
                    <Col lg={9}>
                      <div>
                        <Field
                          name="SAS_TOKEN"
                          placeHolder="SAS Token"
                          component={YBControlledTextInput}
                          onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
                            setFieldValue(`multi_regions.${0}.sas_token`, e.target.value);
                          }}
                          val={values?.multi_regions?.[0]?.sas_token ?? ''}
                        />
                        {touched?.multi_regions?.[0]?.sas_token && (
                          <span className="field-error">
                            {errors?.multi_regions?.[0]?.sas_token}
                          </span>
                        )}
                      </div>
                    </Col>
                  </Row>
                  <Row className="config-provider-row">
                    <Col lg={2} className="az-bucket-label form-item-custom-label">
                      <div>Container URL</div>
                    </Col>
                    <Col lg={9} className="az-multi-region-disabled">
                      <div className="multi-region-disabled-fields az">
                        {getContainerTokenAndFolder(
                          values.multi_regions[0],
                          0,
                          () => {},
                          (fieldName: string, val: any) => {
                            setFieldValue(fieldName, val);
                          },
                          false,
                          false,
                          isEditMode,
                          false,
                          errors?.multi_regions?.[0],
                          touched?.multi_regions?.[0]
                        )}
                      </div>
                    </Col>
                  </Row>
                </>
              )}
              {values.MULTI_REGION_AZ_ENABLED && (
                <div className="multi-region-enabled with-border az">
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
                                regionsInAzure,
                                () => {
                                  arrayHelper.remove(index);
                                },
                                (fieldName: string, val: any) => {
                                  setFieldValue(fieldName, val);
                                },
                                isEditMode,
                                values['default_bucket'] === String(index),
                                errors?.multi_regions?.[index],
                                touched?.multi_regions?.[index]
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
  regionsInAZ: OptionTypeBase[],
  onremoveField: Function,
  updateFieldVal: Function,
  isDisabled = false,
  isdefaultBucket = false,
  errors: Record<string, any> | undefined,
  touched: Record<string, boolean> | Record<string, any>
) => (
  <div className="multi-region-enabled-fields az">
    <div>
      <Field
        name={`multi_regions.${index}.region`}
        component={YBReduxFormSelect}
        options={regionsInAZ}
        label="Region"
        className="field-region"
        placeholder="Select region"
        value={field?.region ?? { value: null, label: null }}
        onChange={(val: any) => {
          updateFieldVal(`multi_regions.${index}.region`, val);
        }}
        isDisabled={isDisabled}
      />
      {touched?.region?.value && <span className="field-error">{errors?.region?.value}</span>}
    </div>
    <div>
      <Field
        name={`multi_regions.${index}.sas_token`}
        component={YBControlledTextInput}
        label="SAS Token"
        placeHolder="SAS Token"
        className="field-SAS-TOKEN"
        onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
          updateFieldVal(`multi_regions.${index}.sas_token`, e.target.value);
        }}
        val={field?.sas_token ?? ''}
        isDisabled={isDisabled}
      />
      {touched?.sas_token && <span className="field-error">{errors?.sas_token}</span>}
    </div>
    <div className="divider" />
    {getContainerTokenAndFolder(
      field,
      index,
      onremoveField,
      updateFieldVal,
      true,
      true,
      isDisabled,
      isdefaultBucket,
      errors,
      touched
    )}
  </div>
);

const getContainerTokenAndFolder = (
  field: configs,
  index: number,
  onremoveField: Function,
  updateFieldVal: Function,
  showCloseIcon = false,
  showDefaultRegionOption = false,
  isDisabled = false,
  isDefaultBucket = false,
  errors: Record<string, any> | undefined,
  touched: Record<string, boolean> | Record<string, any>
) => (
  <>
    <div>
      <Field
        name={`multi_regions.${index}.container`}
        component={YBControlledTextInput}
        label="Container URL"
        className="field-container"
        placeHolder="Container URL"
        onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
          updateFieldVal(`multi_regions.${index}.container`, e.target.value);
        }}
        val={field?.container ?? ''}
        insetError="Error"
        isReadOnly={isDisabled}
        isDisabled={isDisabled}
      />
      {touched?.container && <span className="field-error">{errors?.container}</span>}
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
