/*
 * Created on Fri Jul 29 2022
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
import { OptionTypeBase } from 'react-select';
import { YBControlledTextInput, YBFormInput, YBFormToggle } from '../../../../common/forms/fields';
import { StorageConfigCreationForm, YBReduxFormSelect } from '../common/StorageConfigCreationForm';
import { IStorageProviders } from '../IStorageConfigs';
import Close from '../../../../universes/images/close.svg';
import { flatten, isEmpty, uniq, uniqBy } from 'lodash';
import { CloudType } from '../../../../../redesign/helpers/dtos';
import { useMutation } from 'react-query';
import { addCustomerConfig, editCustomerConfig } from '../common/StorageConfigApi';
import { toast } from 'react-toastify';
import * as Yup from 'yup';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';
import './CreateNFSConfigForm.scss';

interface CreateNFSConfigFormProps {
  visible: boolean;
  editInitialValues: Record<string, any>;
  onHide: () => void;
  fetchConfigs: () => void;
}

type configs = {
  region: OptionTypeBase;
  location: string;
};

interface InitialValuesTypes {
  NFS_CONFIGURATION_NAME: string;
  NFS_BACKUP_LOCATION: string;
  multi_regions: configs[];
  MULTI_REGION_NFS_ENABLED: boolean;
  default_location: string;
}

const MUTLI_REGION_DEFAULT_VALUES = {
  location: '',
  region: { value: null, label: null }
};

export const CreateNFSConfigForm: FC<CreateNFSConfigFormProps> = ({
  visible,
  editInitialValues,
  onHide,
  fetchConfigs
}) => {
  const nfsProviders = useSelector((state: any) =>
    state.cloud.providers.data
      .filter((p: any) => p.code === CloudType.cloud)
      .map((t: any) => t.regions)
  );

  const isEditMode = !isEmpty(editInitialValues);

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

  if (!visible) return null;

  const initialValues: InitialValuesTypes = {
    NFS_CONFIGURATION_NAME: '',
    NFS_BACKUP_LOCATION: '',
    MULTI_REGION_NFS_ENABLED: false,
    multi_regions: [MUTLI_REGION_DEFAULT_VALUES],
    default_location: '0'
  };

  if (isEditMode) {
    initialValues['NFS_CONFIGURATION_NAME'] = editInitialValues['configName'];
    initialValues['NFS_BACKUP_LOCATION'] = editInitialValues.data['BACKUP_LOCATION'];
  }

  const validationSchema = Yup.object().shape({
    NFS_CONFIGURATION_NAME: Yup.string().required('Configuration name is required'),
    NFS_BACKUP_LOCATION: Yup.string().when('MULTI_REGION_NFS_ENABLED', {
      is: (enabled) => !enabled,
      then: Yup.string().required('Backup location is required')
    }),
    multi_regions: Yup.array().when('MULTI_REGION_NFS_ENABLED', {
      is: (enabled) => enabled,
      then: Yup.array()
        .of(
          Yup.object().shape({
            region: Yup.object().shape({
              value: Yup.string().required().typeError('Region is required')
            }),
            location: Yup.string().required('Location is required')
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
  });

  const onSubmit = (values: InitialValuesTypes | FormikValues, _: any) => {
    const payload = {
      type: 'STORAGE',
      name: IStorageProviders.NFS.toUpperCase(),
      configName: values['NFS_CONFIGURATION_NAME'],
      data: {
        BACKUP_LOCATION: values['NFS_BACKUP_LOCATION']
      }
    };

    if (values['MULTI_REGION_NFS_ENABLED']) {
      payload['data']['REGION_LOCATIONS'] = values['multi_regions'].map((r: configs) => {
        return {
          REGION: r.region.value,
          LOCATION: r.location
        };
      });

      // if multi_region is enabled, then get the option selected , else default to the first one.
      const default_location_index = values['MULTI_REGION_NFS_ENABLED']
        ? parseInt(values['default_location'])
        : 0;

      payload['data'][
        'BACKUP_LOCATION'
      ] = `${values['multi_regions'][default_location_index].location}`;
    }

    if (isEditMode) {
      doUpdateStorageConfig.mutate({ configUUID: editInitialValues['configUUID'], ...payload });
    } else {
      doAddStorageConfig.mutate(payload);
    }
  };

  const regionsInNFS: OptionTypeBase[] = uniqBy(
    flatten(nfsProviders)?.map((r: any) => {
      return { value: r.code, label: r.code };
    }) ?? [],
    'label'
  );

  return (
    <StorageConfigCreationForm
      initialValues={initialValues}
      validationSchema={validationSchema}
      type="CREATE"
      onSubmit={onSubmit}
      onCancel={onHide}
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
                    name="NFS_CONFIGURATION_NAME"
                    placeHolder="Configuration Name"
                    component={YBFormInput}
                    onValueChanged={(value: string) =>
                      setFieldValue('NFS_CONFIGURATION_NAME', value)
                    }
                  />
                </Col>
              </Row>
              <Row className="config-provider-row">
                <Col lg={2} className="form-item-custom-label">
                  <div>Multi Region Support</div>
                </Col>
                <Col lg={9}>
                  <Field
                    name="MULTI_REGION_NFS_ENABLED"
                    component={YBFormToggle}
                    isReadOnly={isEditMode}
                    subLabel="Specify a storage path for each region"
                  />
                </Col>
              </Row>
              {!values.MULTI_REGION_NFS_ENABLED && (
                <Row className="config-provider-row">
                  <Col lg={2} className="form-item-custom-label">
                    <div>NFS Storage Path</div>
                  </Col>
                  <Col lg={9}>
                    <Field
                      name="NFS_BACKUP_LOCATION"
                      placeHolder="NFS Storage Path"
                      component={YBControlledTextInput}
                      onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) =>
                        setFieldValue('NFS_BACKUP_LOCATION', e.target.value)
                      }
                      val={values['NFS_BACKUP_LOCATION']}
                      isReadOnly={isEditMode}
                    />
                    {errors.NFS_BACKUP_LOCATION && (
                      <span className="field-error">{errors.NFS_BACKUP_LOCATION}</span>
                    )}
                  </Col>
                </Row>
              )}
              {values.MULTI_REGION_NFS_ENABLED && (
                <div className="multi-region-enabled with-border nfs">
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
                                regionsInNFS,
                                () => {
                                  arrayHelper.remove(index);
                                },
                                (fieldName: string, val: any) => {
                                  setFieldValue(fieldName, val);
                                },
                                isEditMode,
                                values['default_location'] === String(index),
                                errors?.multi_regions?.[index]
                              )}
                            </Col>
                          </Row>
                        ))}
                        <a
                          href="#!"
                          className="on-prem-add-link add-region-link"
                          onClick={(e) => {
                            e.preventDefault();
                            arrayHelper.push({ ...MUTLI_REGION_DEFAULT_VALUES });
                          }}
                        >
                          <i className="fa fa-plus-circle" />
                          Add Region
                        </a>
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
  regionsInGcp: OptionTypeBase[],
  onremoveField: Function,
  updateFieldVal: Function,
  isDisabled = false,
  isdefaultBucket = false,
  errors: Record<string, any> | undefined
) => (
  <div className="multi-region-enabled-fields">
    <div>
      <Field
        name={`multi_regions.${index}.region`}
        component={YBReduxFormSelect}
        options={regionsInGcp}
        label="Region"
        className="field-region"
        value={field?.region ?? { value: null, label: null }}
        onChange={(val: any) => {
          updateFieldVal(`multi_regions.${index}.region`, val);
        }}
        isDisabled={isDisabled}
      />
      <span className="field-error">{errors?.region?.value}</span>
    </div>
    <div className="nfs-location">
      <Field
        name={`multi_regions.${index}.location`}
        placeHolder="NFS Storage Path"
        label="NFS Storage Path"
        component={YBFormInput}
        onValueChanged={(value: string) => updateFieldVal(`multi_regions.${index}.location`, value)}
        isReadOnly={isDisabled}
      />
      <div className="is-default-location">
        <Field
          name="default_location"
          component="input"
          type="radio"
          label="Set as default bucket"
          value={index}
          disabled={isDisabled}
          className="default-location"
          checked={isdefaultBucket}
        />
        Set as default bucket
      </div>
    </div>
    <img
      alt="Remove"
      className="remove-field-icon"
      src={Close}
      width="22"
      onClick={() => !isDisabled && onremoveField()}
    />
  </div>
);
