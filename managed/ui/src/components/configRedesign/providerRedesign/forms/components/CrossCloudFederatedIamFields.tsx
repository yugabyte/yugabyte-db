/*
 * Copyright 2026 YugaByte, Inc. and Contributors
 */
import { Control, useFormState, useWatch } from 'react-hook-form';
import { boolean, string } from 'yup';
import { FormHelperText } from '@material-ui/core';
import { useQuery } from 'react-query';

import { api, runtimeConfigQueryKey } from '../../../../../redesign/helpers/api';
import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '../../../../../actions/customers';
import { isCrossCloudFederatedIamEnabled } from '../../../../backupv2/common/BackupUtils';

import { FieldLabel } from './FieldLabel';
import { FormField } from './FormField';
import { YBInputField, YBToggleField } from '../../../../../redesign/components';

/**
 * Cross-cloud federated IAM fields, shared by every provider form.
 *
 * <p>Presented as one opt-in per storage cloud rather than as a "direction": the operator picks the
 * buckets their nodes must reach, and which way a given node federates follows from the cloud that
 * node runs on. Asking the operator to state a direction invites them to get it backwards, and
 * naming the cloud in the heading removes any doubt about which audience a field wants.
 *
 * <p>A provider only ever shows the clouds its nodes are NOT on - same-cloud access is native. An
 * on-prem provider shows both, because its nodes may run on either.
 */
export interface CrossCloudFederatedIamFormValues {
  enableFederatedIam?: boolean;
  federationGcsEnabled?: boolean;
  federationGcsAudience?: string;
  federationS3Enabled?: boolean;
  federationS3RoleArn?: string;
  federationS3Audience?: string;
}

/**
 * Client-side mirror of OnPremValidator's rules, so the operator sees a malformed audience or ARN
 * before saving rather than as a server-side rejection. Defined once and shared by the create and
 * edit forms; the server remains the authority.
 */
export const FEDERATION_AUDIENCE_REGEX = /^[A-Za-z0-9._:/-]{1,512}$/;
export const FEDERATION_ROLE_ARN_REGEX = /^arn:aws:iam::[0-9]{12}:role\/[A-Za-z0-9._/+=,@-]{1,256}$/;

export const FEDERATED_IAM_VALIDATION = {
  enableFederatedIam: boolean().test(
    'at-least-one-target',
    'Select at least one storage cloud your nodes must reach.',
    function (enabled) {
      const { federationGcsEnabled, federationS3Enabled } = this.parent ?? {};
      return !enabled || !!federationGcsEnabled || !!federationS3Enabled;
    }
  ),
  federationGcsAudience: string().when(['enableFederatedIam', 'federationGcsEnabled'], {
    is: (enableFederatedIam: boolean, federationGcsEnabled: boolean) =>
      !!enableFederatedIam && !!federationGcsEnabled,
    then: string()
      .required('GCS audience is required.')
      .matches(FEDERATION_AUDIENCE_REGEX, 'GCS audience contains unsupported characters.')
  }),
  federationS3RoleArn: string().when(['enableFederatedIam', 'federationS3Enabled'], {
    is: (enableFederatedIam: boolean, federationS3Enabled: boolean) =>
      !!enableFederatedIam && !!federationS3Enabled,
    then: string()
      .required('S3 role ARN is required.')
      .matches(
        FEDERATION_ROLE_ARN_REGEX,
        'Expected arn:aws:iam::<12-digit-account>:role/<role-name>.'
      )
  }),
  federationS3Audience: string().when(['enableFederatedIam', 'federationS3Enabled'], {
    is: (enableFederatedIam: boolean, federationS3Enabled: boolean) =>
      !!enableFederatedIam && !!federationS3Enabled,
    then: string()
      .required('S3 audience is required.')
      .matches(FEDERATION_AUDIENCE_REGEX, 'S3 audience contains unsupported characters.')
  })
};

export interface CrossCloudFederationTargetValue {
  targetCloud: 'gcp' | 'aws';
  audience: string;
  roleArn?: string;
}

/** Form values -> the provider's crossCloudFederationTargets list, one entry per enabled storage cloud. */
export const buildFederationTargets = (
  formValues: CrossCloudFederatedIamFormValues
): CrossCloudFederationTargetValue[] => {
  const targets: CrossCloudFederationTargetValue[] = [];
  if (formValues.federationGcsEnabled && formValues.federationGcsAudience) {
    targets.push({ targetCloud: 'gcp', audience: formValues.federationGcsAudience });
  }
  if (formValues.federationS3Enabled && formValues.federationS3Audience) {
    targets.push({
      targetCloud: 'aws',
      audience: formValues.federationS3Audience,
      roleArn: formValues.federationS3RoleArn
    });
  }
  return targets;
};

/**
 * Provider -> form values, including the deprecated flat fields so a provider configured before
 * crossCloudFederationTargets opens with its settings shown rather than blank.
 *
 * <p>The flat audience means a different target depending on the provider: an AWS provider's nodes
 * could only ever have federated to GCS, a GCP provider's only to S3.
 */
export const federationFormValuesFromProvider = (
  cloudInfo: any,
  providerCloud?: 'aws' | 'gcp'
): CrossCloudFederatedIamFormValues => {
  const targets: CrossCloudFederationTargetValue[] = cloudInfo?.crossCloudFederationTargets ?? [];
  const gcs = targets.find((target) => target.targetCloud === 'gcp');
  const s3 = targets.find((target) => target.targetCloud === 'aws');
  const legacyAudience = cloudInfo?.federatedIamAudience;
  const legacyRoleArn = cloudInfo?.federatedIamRoleArn;
  // A GCP provider's nodes run on GCP, so its legacy fields described the S3 direction.
  const legacyIsS3 = providerCloud === 'gcp';
  return {
    enableFederatedIam: cloudInfo?.enableFederatedIam ?? false,
    federationGcsEnabled: !!gcs || (!!legacyAudience && !legacyIsS3),
    federationGcsAudience: gcs?.audience ?? (legacyIsS3 ? '' : legacyAudience ?? ''),
    federationS3Enabled: !!s3 || (!!legacyAudience && legacyIsS3),
    federationS3RoleArn: s3?.roleArn ?? (legacyIsS3 ? legacyRoleArn ?? '' : ''),
    federationS3Audience: s3?.audience ?? (legacyIsS3 ? legacyAudience ?? '' : '')
  };
};

interface CrossCloudFederatedIamFieldsProps {
  control: Control<any>;
  isFormDisabled: boolean;
  /**
   * Cloud this provider's nodes run on. That cloud's own storage is reached natively, so its block
   * is hidden. Omitted for on-prem, whose nodes may be on any cloud, so both blocks show.
   */
  providerCloud?: 'aws' | 'gcp';
}

export const CrossCloudFederatedIamFields = ({
  control,
  isFormDisabled,
  providerCloud
}: CrossCloudFederatedIamFieldsProps) => {
  // Every hook runs unconditionally; the preview gate is applied only after them, because a
  // conditional hook changes call order between renders.
  const runtimeConfigQuery = useQuery(runtimeConfigQueryKey.globalScope(), () =>
    api.fetchRuntimeConfigs(DEFAULT_RUNTIME_GLOBAL_SCOPE, true)
  );
  const { errors } = useFormState({ control, name: 'enableFederatedIam' });
  const enableFederatedIam = useWatch({ control, name: 'enableFederatedIam' });
  const federationGcsEnabled = useWatch({ control, name: 'federationGcsEnabled' });
  const federationS3Enabled = useWatch({ control, name: 'federationS3Enabled' });

  // Preview feature: a provider that already carries federation settings keeps them - this gates
  // the form, not the behaviour.
  if (!isCrossCloudFederatedIamEnabled(runtimeConfigQuery.data)) {
    return null;
  }

  return (
    <>
      <FormField>
        <FieldLabel
          infoTitle="Federated IAM"
          infoContent="Lets this provider's DB nodes back up to a cloud they do not run on, using each node's own cloud identity instead of stored keys. YBA detects which cloud each node runs on, so you only choose the storage clouds your nodes must reach."
        >
          Enable Federated IAM
        </FieldLabel>
        <YBToggleField name="enableFederatedIam" control={control} disabled={isFormDisabled} />
      </FormField>
      {!!errors?.enableFederatedIam?.message && (
        <FormHelperText error={true}>
          {errors.enableFederatedIam.message as string}
        </FormHelperText>
      )}
      {enableFederatedIam && (
        <>
          {providerCloud !== 'gcp' && (
            <>
          <FormField>
            <FieldLabel
              infoTitle="Google Cloud Storage"
              infoContent="Turn this on if any of this provider's nodes back up to a GCS bucket. It applies to nodes running on AWS; a node already running on GCP reaches GCS natively and is left alone."
            >
              Access Google Cloud Storage (GCS)
            </FieldLabel>
            <YBToggleField
              name="federationGcsEnabled"
              control={control}
              disabled={isFormDisabled}
            />
          </FormField>
          {federationGcsEnabled && (
            <FormField>
              <FieldLabel>GCS Workload Identity Audience</FieldLabel>
              <YBInputField
                control={control}
                name="federationGcsAudience"
                disabled={isFormDisabled}
                placeholder="//iam.googleapis.com/projects/<n>/locations/global/workloadIdentityPools/<pool>/providers/<provider>"
                fullWidth
              />
            </FormField>
          )}
            </>
          )}
          {providerCloud !== 'aws' && (
            <>
          <FormField>
            <FieldLabel
              infoTitle="Amazon S3"
              infoContent="Turn this on if any of this provider's nodes back up to an S3 bucket. It applies to nodes running on GCP; a node already running on AWS reaches S3 natively and is left alone."
            >
              Access Amazon S3
            </FieldLabel>
            <YBToggleField name="federationS3Enabled" control={control} disabled={isFormDisabled} />
          </FormField>
          {federationS3Enabled && (
            <>
              <FormField>
                <FieldLabel>S3 Role ARN</FieldLabel>
                <YBInputField
                  control={control}
                  name="federationS3RoleArn"
                  disabled={isFormDisabled}
                  placeholder="arn:aws:iam::<account>:role/<role>"
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>S3 Web-Identity Audience</FieldLabel>
                <YBInputField
                  control={control}
                  name="federationS3Audience"
                  disabled={isFormDisabled}
                  placeholder="//iam.googleapis.com/projects/<n>/locations/global/workloadIdentityPools/<pool>/providers/<provider>"
                  fullWidth
                />
              </FormField>
            </>
          )}
        </>
      )}
            </>
          )}
    </>
  );
};
