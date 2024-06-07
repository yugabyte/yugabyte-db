/*
 * Created on Mon Apr 18 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import { Field as ReduxField } from 'redux-form';
import { YBLabel } from '../../../common/descriptors';
import { Field as FormikField } from 'formik';

import { useSelector } from 'react-redux';
import { flatten, uniq } from 'lodash';
import { YBMultiEntryInput } from '../../../common/forms/fields/YBMultiEntryInput';
import './NTPConfig.scss';

export const IP_V4_REGEX = /((\d+\.){3}\d+)/;
export const IP_V6_REGEX = /(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))/;
export const HOSTNAME_REGEX = /^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9-]*[a-zA-Z0-9])\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9-]*[A-Za-z0-9])$/;

const isValid_IP_or_Hostname = (values: string[]) => {
  if (!Array.isArray(values)) {
    return;
  }
  return values.every((v) => IP_V4_REGEX.test(v) || IP_V6_REGEX.test(v) || HOSTNAME_REGEX.test(v))
    ? undefined
    : 'Invalid Hostname';
};

export enum NTP_TYPES {
  PROVIDER = 'provider',
  MANUAL = 'manual',
  NO_NTP = 'no-ntp'
}

export enum FIELD_TYPE {
  REDUX = 'redux',
  FORMIK = 'formik'
}

const NTP_OPTIONS = [
  {
    label: "Use provider's  NTP server",
    value: NTP_TYPES.PROVIDER
  },
  {
    label: 'Manually add NTP Servers',
    value: NTP_TYPES.MANUAL
  },
  {
    label: "Don't setup NTP",
    value: NTP_TYPES.NO_NTP
  }
];

const NO_NTP_WARNING = (
  <div className="no-ntp-warning">
    <i className="fa fa-warning" />
    <span>
      <b>Note!</b> Ensure that NTP is configured on your machine image, otherwise YugabyteDB will
      not be able to preserve data consistency
    </span>
  </div>
);

interface NTPConfigProps {
  onChange: Function;
  hideHelp?: boolean;
  fieldType: FIELD_TYPE;
  hideOnPremProvider: boolean;
  initialValues: Record<string, any>;
  disabled: boolean;
}

const GET_FIELD = (type: FIELD_TYPE, props: any) =>
  type === FIELD_TYPE.REDUX ? <ReduxField {...props} /> : <FormikField {...props} />;

export const NTPConfig: FC<NTPConfigProps> = ({
  onChange,
  hideHelp = false,
  fieldType = FIELD_TYPE.REDUX,
  hideOnPremProvider = false,
  initialValues = {},
  disabled = false
}) => {
  const access_keys = useSelector((state: any) => state.cloud.accessKeys);
  const isAllReqCompleted = useSelector((state: any) => state.cloud.allAccessKeysReqCompleted);
  const configured_ntp_servers = uniq(
    flatten(
      access_keys?.data
        .map((info: any) => info?.keyInfo?.ntpServers)
        .filter((ntp: any) => Array.isArray(ntp) && ntp.length > 0)
    )
  );

  const tooltipContent =
    "Network Time Protocol (NTP) synchronizes machine's times across a network";

  const [selectedNTPOption, setSelectedNTPOption] = useState(
    initialValues.ntp_option ?? NTP_OPTIONS[0].value
  );

  return (
    <div className={`ntp-row ${hideHelp ? 'show-margin' : ''}`}>
      <YBLabel infoContent={!hideHelp && tooltipContent} infoTitle={!hideHelp && 'NTP'}>
        {NTP_OPTIONS.map((target) => {
          if (hideOnPremProvider && target.value === NTP_TYPES.PROVIDER) return null;
          return (
            <label className="btn-group btn-group-radio" key={target.value}>
              {GET_FIELD(fieldType, {
                name: 'ntp_option',
                component: 'input',
                onChange: (formikVal: any, reduxVal: any) => {
                  const val = fieldType === FIELD_TYPE.FORMIK ? formikVal.target.value : reduxVal;
                  setSelectedNTPOption(val);

                  onChange('setUpChrony', val !== NTP_TYPES.NO_NTP);
                  onChange('ntp_option', val);
                  if (val !== NTP_TYPES.MANUAL) {
                    onChange('ntpServers', []);
                  }
                },
                type: 'radio',
                checked: selectedNTPOption === target.value,
                value: target.value,
                disabled
              })}
              {target.label}
            </label>
          );
        })}
      </YBLabel>
      {selectedNTPOption === NTP_TYPES.MANUAL && (
        <div className="manual-ntp">
          <div className="info-text">Specify NTP servers you want to use</div>
          {GET_FIELD(fieldType, {
            name: 'ntpServers',
            // eslint-disable-next-line react/display-name
            component: (field: any) => {
              const values =
                fieldType === FIELD_TYPE.REDUX ? field.input.value : field.form.values.ntpServers;
              return (
                <>
                  <YBMultiEntryInput
                    isDisabled={disabled || !isAllReqCompleted}
                    isLoading={!isAllReqCompleted}
                    placeholder={
                      isAllReqCompleted ? 'Add NTP Servers' : 'Loading NTP servers list...'
                    }
                    defaultOptions={configured_ntp_servers.map((t: any) => {
                      return { value: t, label: t };
                    })}
                    delimiters={[',', ' ']}
                    components={{
                      DropdownIndicator: null
                    }}
                    onChange={(val) => {
                      onChange('ntpServers', val ? val.map((v) => v.value) : []);
                      if (fieldType === FIELD_TYPE.FORMIK) {
                        field.form.setTouched({ ...field?.form.touched, ntpServers: true });
                      }
                    }}
                    val={values?.map((t: string) => {
                      return { value: t, label: t };
                    })}
                  />
                  {/* Redux-form error */}
                  {field?.meta?.touched && field?.meta?.error && (
                    <span className="standard-error">{field.meta.error}</span>
                  )}
                  {/* Formik Error */}
                  {field?.form?.touched?.ntpServers && field?.form?.errors['ntpServers'] && (
                    <span className="standard-error">{field.form.errors['ntpServers']}</span>
                  )}
                </>
              );
            },
            validate: isValid_IP_or_Hostname
          })}
          {!disabled && (
            <div className="sub-text">Separate new entries by `whitespace` or `comma`</div>
          )}
        </div>
      )}
      {selectedNTPOption === NTP_TYPES.NO_NTP && NO_NTP_WARNING}
    </div>
  );
};
