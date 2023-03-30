import {
  HOSTNAME_REGEX,
  IP_V4_REGEX,
  IP_V6_REGEX
} from '../../../config/PublicCloud/views/NTPConfig';

export const NTP_SERVER_REGEX = new RegExp(
  `(${IP_V4_REGEX.source})|(${IP_V6_REGEX.source})|(${HOSTNAME_REGEX.source})`
);
