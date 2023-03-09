import {
  HOSTNAME_REGEX,
  IP_V4_REGEX,
  IP_V6_REGEX
} from '../../../config/PublicCloud/views/NTPConfig';
import { AWSValidationKey } from '../constants';
import { AWSProviderCreateFormFieldValues } from './aws/AWSProviderCreateForm';

export const NTP_SERVER_REGEX = new RegExp(
  `(${IP_V4_REGEX.source})|(${IP_V6_REGEX.source})|(${HOSTNAME_REGEX.source})`
);

export const AWS_INVALID_FIELDS: Record<
  AWSValidationKey,
  readonly (keyof AWSProviderCreateFormFieldValues)[]
> = {
  [AWSValidationKey.ACCESS_KEY_CREDENTIALS]: ['accessKeyId', 'secretAccessKey'],
  [AWSValidationKey.HOSTED_ZONE_ID]: ['hostedZoneId'],
  [AWSValidationKey.IAM_CREDENTIALS]: ['providerCredentialType'],
  [AWSValidationKey.NTP_SERVERS]: ['ntpServers'],
  [AWSValidationKey.SSH_PRIVATE_KEY_CONTENT]: ['sshPrivateKeyContent'],
  [AWSValidationKey.REGION]: ['regions']
} as const;
