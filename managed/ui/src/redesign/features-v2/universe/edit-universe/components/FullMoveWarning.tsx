import { Trans, useTranslation } from 'react-i18next';
import { AlertVariant, mui, YBAlert } from '@yugabyte-ui-library/core';

const { Box } = mui;

type FullMoveSetting = 'publicIp' | 'networkPorts' | 'instanceProfileArn';

interface FullMoveWarningProps {
  setting: FullMoveSetting;
}

export const FullMoveWarning = ({ setting }: FullMoveWarningProps) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.fullMoveWarning' });

  return (
    <Box mt={2}>
      <YBAlert
        open
        variant={AlertVariant.Warning}
        text={
          <Trans
            t={t}
            i18nKey="message"
            values={{ setting: t(setting) }}
            components={{ bold: <strong />, br: <br /> }}
          />
        }
      />
    </Box>
  );
};
