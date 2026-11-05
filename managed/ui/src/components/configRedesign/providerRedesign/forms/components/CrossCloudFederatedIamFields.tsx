/*
 * Copyright 2026 YugaByte, Inc. and Contributors
 */
import { ReactNode } from 'react';
import clsx from 'clsx';
import { useDropzone } from 'react-dropzone';
import { Control, useController, useFormContext, useFormState, useWatch } from 'react-hook-form';
import { mixed, string } from 'yup';
import { Divider, FormHelperText, makeStyles, Typography } from '@material-ui/core';
import { useQuery } from 'react-query';
import { YBButton, YBInputField, YBToggleField } from '@yugabyte-ui-library/core';

import { api, runtimeConfigQueryKey } from '../../../../../redesign/helpers/api';
import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '../../../../../actions/customers';
import { isCrossCloudFederatedIamEnabled } from '../../../../backupv2/common/BackupUtils';
import { readFileAsText } from '../utils';

import { FieldGroup } from './FieldGroup';
import { FieldLabel } from './FieldLabel';
import { FormField } from './FormField';

import ExternalLinkIcon from '../../../../../redesign/assets/external-link-small.svg';

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
 *
 * <p>Federation is on for the provider exactly when at least one storage cloud is turned on, so
 * there is no separate master switch.
 */
export interface CrossCloudFederatedIamFormValues {
  federationGcsEnabled?: boolean;
  // Read in the browser only for its audience; the file itself is never sent to YBA.
  federationGcsCredentialFile?: File;
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
export const FEDERATION_ROLE_ARN_REGEX =
  /^arn:aws:iam::[0-9]{12}:role\/[A-Za-z0-9._/+=,@-]{1,256}$/;

/**
 * Google workload identity credential configuration file -> its audience, or why it can't be used.
 *
 * <p>YBA renders the node's external_account JSON itself from the audience alone
 * (gcp-fed-creds.json.j2), so a file that impersonates a service account is rejected: accepting
 * it would silently run backups as the federated principal instead of that service account.
 */
export const parseGcsCredentialConfig = (
  text: string | null
): { audience: string; error?: undefined } | { audience?: undefined; error: string } => {
  let config: any;
  try {
    config = JSON.parse(text ?? '');
  } catch {
    return { error: 'The file is not valid JSON.' };
  }
  if (config?.type !== 'external_account') {
    return {
      error:
        'This is not a workload identity credential configuration file. Download it from the workload identity pool in Google Cloud.'
    };
  }
  if (config.service_account_impersonation_url) {
    return {
      error:
        'Service account impersonation is not supported. Download the credential configuration without impersonation.'
    };
  }
  if (typeof config.audience !== 'string' || !FEDERATION_AUDIENCE_REGEX.test(config.audience)) {
    return { error: 'The file has no valid audience.' };
  }
  return { audience: config.audience };
};

export const FEDERATED_IAM_VALIDATION = {
  federationGcsCredentialFile: mixed().test(
    'gcs-credential-config',
    'Invalid credential configuration file.',
    async function (file) {
      if (!file) {
        return true;
      }
      const { error } = parseGcsCredentialConfig(await readFileAsText(file));
      return error ? this.createError({ message: error }) : true;
    }
  ),
  federationGcsAudience: string().when('federationGcsEnabled', {
    is: true,
    then: string()
      .required('Upload the credential configuration file.')
      .matches(FEDERATION_AUDIENCE_REGEX, 'GCS audience contains unsupported characters.')
  }),
  federationS3RoleArn: string().when('federationS3Enabled', {
    is: true,
    then: string()
      .required('IAM role ARN is required.')
      .matches(
        FEDERATION_ROLE_ARN_REGEX,
        'Expected arn:aws:iam::<12-digit-account>:role/<role-name>.'
      )
  }),
  federationS3Audience: string().when('federationS3Enabled', {
    is: true,
    then: string()
      .required('Audience is required.')
      .matches(FEDERATION_AUDIENCE_REGEX, 'Audience contains unsupported characters.')
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
 *
 * <p>A provider that has targets but enableFederatedIam off opens with its clouds turned off, since
 * saving the form now turns federation on whenever a cloud is on.
 */
export const federationFormValuesFromProvider = (
  cloudInfo: any,
  providerCloud?: 'aws' | 'gcp'
): CrossCloudFederatedIamFormValues => {
  const enabled = !!cloudInfo?.enableFederatedIam;
  const targets: CrossCloudFederationTargetValue[] = cloudInfo?.crossCloudFederationTargets ?? [];
  const gcs = targets.find((target) => target.targetCloud === 'gcp');
  const s3 = targets.find((target) => target.targetCloud === 'aws');
  const legacyAudience = cloudInfo?.federatedIamAudience;
  const legacyRoleArn = cloudInfo?.federatedIamRoleArn;
  // A GCP provider's nodes run on GCP, so its legacy fields described the S3 direction.
  const legacyIsS3 = providerCloud === 'gcp';
  return {
    federationGcsEnabled: enabled && (!!gcs || (!!legacyAudience && !legacyIsS3)),
    federationGcsAudience: gcs?.audience ?? (legacyIsS3 ? '' : (legacyAudience ?? '')),
    federationS3Enabled: enabled && (!!s3 || (!!legacyAudience && legacyIsS3)),
    federationS3RoleArn: s3?.roleArn ?? (legacyIsS3 ? (legacyRoleArn ?? '') : ''),
    federationS3Audience: s3?.audience ?? (legacyIsS3 ? (legacyAudience ?? '') : '')
  };
};

/** Provider payload fields for federation, derived from which storage clouds are turned on. */
export const buildFederationPayload = (formValues: CrossCloudFederatedIamFormValues) => {
  const crossCloudFederationTargets = buildFederationTargets(formValues);
  return crossCloudFederationTargets.length > 0
    ? { enableFederatedIam: true, crossCloudFederationTargets }
    : { enableFederatedIam: false };
};

// Sizes and colours follow the Cross-Cloud Federated IAM Figma (section 2).
const useStyles = makeStyles((theme) => ({
  text: {
    color: theme.palette.grey[900],
    fontSize: 13,
    // The theme's body1 Typography is semibold; the Figma body text is regular.
    fontWeight: 400,
    lineHeight: '16px'
  },
  semiBold: {
    fontWeight: 600
  },
  sectionHeading: {
    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 600,
    lineHeight: '16px',
    marginBottom: theme.spacing(1)
  },
  divider: {
    margin: theme.spacing(3, 0),
    backgroundColor: theme.palette.grey[300]
  },
  note: {
    marginBottom: theme.spacing(3)
  },
  cards: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3),
    width: '100%'
  },
  card: {
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: 8
  },
  cardHeader: {
    display: 'flex',
    alignItems: 'center',
    height: 52,
    padding: theme.spacing(0, 2)
  },
  cardBody: {
    display: 'flex',
    flexDirection: 'column',
    padding: theme.spacing(2),
    borderTop: `1px solid ${theme.palette.grey[300]}`
  },
  s3CardBody: {
    gap: theme.spacing(3)
  },
  gcsCardBody: {
    gap: theme.spacing(1)
  },
  dropZone: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    gap: theme.spacing(1),
    padding: theme.spacing(2),
    backgroundColor: theme.palette.common.white,
    border: `1px dashed ${theme.palette.grey[400]}`,
    borderRadius: 8
  },
  dropZoneActive: {
    borderColor: theme.palette.primary[600]
  },
  dropZoneOr: {
    color: theme.palette.grey[700]
  },
  configuredAudience: {
    color: theme.palette.grey[700],
    wordBreak: 'break-all'
  },
  setupGuide: {
    display: 'flex',
    flexWrap: 'wrap',
    alignItems: 'center',
    gap: theme.spacing(0.5)
  },
  setupGuideLink: {
    display: 'inline-flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),
    color: theme.palette.primary[600],
    fontSize: 13,
    lineHeight: '16px',
    textDecoration: 'underline',
    '&:hover, &:focus': {
      color: theme.palette.primary[600]
    }
  }
}));

/**
 * Preview gate (yb.ui.feature_flags.enable_cross_cloud_federated_iam) for every federated IAM control
 * in the provider forms. A provider that already carries federation settings keeps them - this gates
 * the form, not the behaviour.
 */
export const useIsCrossCloudFederatedIamUiEnabled = () => {
  const runtimeConfigQuery = useQuery(runtimeConfigQueryKey.globalScope(), () =>
    api.fetchRuntimeConfigs(DEFAULT_RUNTIME_GLOBAL_SCOPE, true)
  );
  return isCrossCloudFederatedIamEnabled(runtimeConfigQuery.data);
};

interface SectionHeadingProps {
  children: ReactNode;
  infoContent: string;
  infoTitle: string;
}

/** "Access to <cloud>" style sub-heading inside a provider's Cloud Info group. */
export const CloudAccessHeading = ({ children, infoContent, infoTitle }: SectionHeadingProps) => {
  const classes = useStyles();
  const isFederatedIamUiEnabled = useIsCrossCloudFederatedIamUiEnabled();
  // The cloud's own access only needs a heading of its own beside "Access to other clouds".
  if (!isFederatedIamUiEnabled) {
    return null;
  }
  return (
    <FieldLabel className={classes.sectionHeading} infoContent={infoContent} infoTitle={infoTitle}>
      {children}
    </FieldLabel>
  );
};

// shortcut: no setup guide page exists yet, so the link goes nowhere; point it at the docs page once
// it is published.
const SetupGuideLink = () => {
  const classes = useStyles();
  return (
    <a href="#" className={classes.setupGuideLink}>
      view the setup guide
      <ExternalLinkIcon />
    </a>
  );
};

interface ServiceCardProps {
  control: Control<any>;
  name: 'federationGcsEnabled' | 'federationS3Enabled';
  label: string;
  disabled: boolean;
  bodyClassName: string;
  children: ReactNode;
}

const ServiceCard = ({
  control,
  name,
  label,
  disabled,
  bodyClassName,
  children
}: ServiceCardProps) => {
  const classes = useStyles();
  const enabled = useWatch({ control, name });
  return (
    <div className={classes.card}>
      <div className={classes.cardHeader}>
        <YBToggleField
          control={control}
          name={name}
          label={label}
          disabled={disabled}
          dataTestId={`CrossCloudFederatedIam-${name}`}
        />
      </div>
      {enabled && <div className={clsx(classes.cardBody, bodyClassName)}>{children}</div>}
    </div>
  );
};

const GcsCardBody = ({ control, disabled }: { control: Control<any>; disabled: boolean }) => {
  const classes = useStyles();
  const { setValue, resetField, trigger } = useFormContext();
  const { field: fileField, fieldState: fileFieldState } = useController({
    control,
    name: 'federationGcsCredentialFile'
  });
  const audience = useWatch({ control, name: 'federationGcsAudience' });
  const { errors } = useFormState({ control, name: 'federationGcsAudience' });
  const errorMessage = fileFieldState.error?.message ?? errors?.federationGcsAudience?.message;

  const onFileChange = async (file: File | undefined) => {
    fileField.onChange(file);
    const parsed = file ? parseGcsCredentialConfig(await readFileAsText(file)) : undefined;
    if (parsed?.audience) {
      setValue('federationGcsAudience', parsed.audience, { shouldValidate: true });
    } else {
      // Back to the audience the provider was saved with (empty on create).
      resetField('federationGcsAudience');
    }
    // Report a bad file as soon as it is dropped, not only on submit.
    trigger('federationGcsCredentialFile');
  };
  const { getRootProps, getInputProps, open, isDragActive } = useDropzone({
    onDropAccepted: (files) => onFileChange(files[0]),
    multiple: false,
    noClick: true,
    noKeyboard: true,
    disabled
  });
  const file: File | undefined = fileField.value;

  return (
    <>
      <Typography className={classes.sectionHeading}>Credential configuration file</Typography>
      {/* Only an audience saved on the provider; once a file is chosen the file is what counts. */}
      {!file && !!audience && (
        <Typography className={clsx(classes.text, classes.configuredAudience)}>
          Configured audience: {audience}
        </Typography>
      )}
      <div
        className={clsx(classes.dropZone, isDragActive && classes.dropZoneActive)}
        {...getRootProps()}
      >
        <input name={fileField.name} {...getInputProps()} />
        <Typography className={classes.text}>{file?.name ?? 'Drag a JSON file here'}</Typography>
        <Typography className={clsx(classes.text, classes.dropZoneOr)}>or</Typography>
        <YBButton
          variant="secondary"
          onClick={open}
          disabled={disabled}
          dataTestId="CrossCloudFederatedIam-federationGcsCredentialFile-browse"
        >
          Browse
        </YBButton>
      </div>
      {/* A bad file and a missing audience are reported on different fields; show whichever applies. */}
      {!!errorMessage && <FormHelperText error={true}>{errorMessage as string}</FormHelperText>}
      <Typography className={classes.text}>
        Download this file from the workload identity pool in Google Cloud. For help,
      </Typography>
      <div>
        <SetupGuideLink />
      </div>
    </>
  );
};

const S3CardBody = ({ control, disabled }: { control: Control<any>; disabled: boolean }) => {
  const classes = useStyles();
  return (
    <>
      <YBInputField
        control={control}
        name="federationS3RoleArn"
        label="IAM role ARN"
        placeholder="arn:aws:iam::123456789012:role/role-name"
        helperText="The AWS IAM role for universe nodes to use to access Amazon S3."
        disabled={disabled}
        fullWidth
        dataTestId="CrossCloudFederatedIam-federationS3RoleArn"
      />
      <YBInputField
        control={control}
        name="federationS3Audience"
        label="Audience"
        placeholder="https://yugabyte.com/yb-s3-federation"
        helperText="The audience that universe nodes request in their Google ID token. Use any URI you control."
        disabled={disabled}
        fullWidth
        dataTestId="CrossCloudFederatedIam-federationS3Audience"
      />
      <div className={classes.setupGuide}>
        <Typography className={classes.text}>
          For help creating the role and its trust policy,
        </Typography>
        <SetupGuideLink />
      </div>
    </>
  );
};

interface CrossCloudFederatedIamFieldsProps {
  control: Control<any>;
  isFormDisabled: boolean;
  /**
   * Cloud this provider's nodes run on. That cloud's own storage is reached natively, so its card
   * is hidden. Omitted for on-prem, whose nodes may be on any cloud, so both cards show in a panel
   * of their own.
   */
  providerCloud?: 'aws' | 'gcp';
}

export const CrossCloudFederatedIamFields = ({
  control,
  isFormDisabled,
  providerCloud
}: CrossCloudFederatedIamFieldsProps) => {
  const classes = useStyles();
  // Every hook runs unconditionally; the preview gate is applied only after them, because a
  // conditional hook changes call order between renders.
  const isFederatedIamUiEnabled = useIsCrossCloudFederatedIamUiEnabled();

  if (!isFederatedIamUiEnabled) {
    return null;
  }

  const services = (
    <FormField>
      <FieldLabel>Services</FieldLabel>
      <div className={classes.cards}>
        {providerCloud !== 'aws' && (
          <ServiceCard
            control={control}
            name="federationS3Enabled"
            label="Amazon S3"
            disabled={isFormDisabled}
            bodyClassName={classes.s3CardBody}
          >
            <S3CardBody control={control} disabled={isFormDisabled} />
          </ServiceCard>
        )}
        {providerCloud !== 'gcp' && (
          <ServiceCard
            control={control}
            name="federationGcsEnabled"
            label="Google Cloud Storage"
            disabled={isFormDisabled}
            bodyClassName={classes.gcsCardBody}
          >
            <GcsCardBody control={control} disabled={isFormDisabled} />
          </ServiceCard>
        )}
      </div>
    </FormField>
  );

  if (!providerCloud) {
    return (
      <FieldGroup heading="Cross Cloud Federated Access">
        <Typography className={clsx(classes.text, classes.note)}>
          <span className={classes.semiBold}>Note:</span> Available only for nodes that run on AWS
          or Google Cloud. Nodes on your own hardware can&apos;t use federated access.
        </Typography>
        {services}
      </FieldGroup>
    );
  }

  return (
    <>
      <Divider className={classes.divider} />
      <CloudAccessHeading
        infoTitle="Access to other clouds"
        infoContent="Enable the cloud services that universe nodes on this provider can reach through federated access."
      >
        Access to other clouds
      </CloudAccessHeading>
      {services}
    </>
  );
};
