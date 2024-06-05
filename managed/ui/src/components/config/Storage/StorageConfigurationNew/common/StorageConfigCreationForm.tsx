/*
 * Created on Fri Jul 22 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React from 'react';
import { Formik, FormikActions, FormikProps, FormikValues } from 'formik';
import Select from 'react-select';
import { YBLabel } from '../../../../common/descriptors';
import { YBButton } from '../../../../common/forms/fields';
import './StorageConfigCreationForm.scss';

type StorageConfigCreationFormProps<T> = {
  initialValues: T;
  type: 'EDIT' | 'CREATE';
  components: (formikValues: FormikProps<FormikValues | T>) => React.ReactNode;
  onSubmit: (values: FormikValues, formikActions: FormikActions<Record<string, any>>) => void;
  validationSchema?: any;
  onCancel: () => void;
};

export const StorageConfigCreationForm = <T extends FormikValues>({
  components,
  onSubmit,
  initialValues,
  validationSchema,
  onCancel
}: StorageConfigCreationFormProps<T>) => {
  return (
    <>
      <Formik
        validationSchema={validationSchema}
        // validateOnChange={false}
        validateOnBlur={false}
        render={(props) => (
          <>
            <form onSubmit={props.handleSubmit}>
              {components(props)}
              <div className="storage-config-form-actions">
                <YBButton btnText="Save" btnClass="btn btn-orange" btnType="submit" />
                <YBButton btnText="Cancel" btnClass="btn" onClick={() => onCancel()} />
              </div>
            </form>
          </>
        )}
        onSubmit={onSubmit}
        initialValues={initialValues}
      />
    </>
  );
};

export const YBReduxFormSelect = (props: { props: any }) => {
  return (
    <YBLabel {...props}>
      <Select
        styles={{
          menu: (provided) => ({ ...provided, zIndex: 2 }),
          control: (provided) => ({ ...provided, height: '42px' })
        }}
        {...props}
      />
    </YBLabel>
  );
};
