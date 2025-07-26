import { FC, useState } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation, Trans } from 'react-i18next';
import { styled } from '@material-ui/core';
import { useFormContext, useWatch } from 'react-hook-form';
import { mui, YBToggleField, YBTooltip } from '@yugabyte-ui-library/core';
import { YBEarlyAccessTag } from '../../../../components';
import { AnalyzeDialog } from '../../../../features/universe/universe-actions/edit-pg-compatibility/AnalyzeDialog';
import { isVersionPGSupported } from '../../../../features/universe/universe-form/utils/helpers';

import { DatabaseSettingsProps } from '../../steps/database-settings/dtos';

const { Box, Typography, Link } = mui;

interface PGCompatibiltyFieldProps {
  disabled: boolean;
  dbVersion: string;
}

const PG_COMPATIBILITY_FIELD = 'enablePGCompatibitilty';

const StyledSubText = styled(Typography)({
  fontSize: '11.5px',
  lineHeight: '16px',
  fontWeight: 400,
  color: '#67666C',
  marginLeft: '8px'
});

const StyledLinkText = styled(Link)({
  fontSize: '11.5px',
  lineHeight: '16px',
  fontWeight: 400,
  color: '#67666C'
});

export const PGCompatibiltyField: FC<PGCompatibiltyFieldProps> = ({ disabled, dbVersion }) => {
  const { control } = useFormContext<DatabaseSettingsProps>();
  const [openAnalyzeModal, setAnalyzeModal] = useState(false);
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.databaseSettings.pgCompatibility'
  });

  //watchers
  const pgValue = useWatch({ name: PG_COMPATIBILITY_FIELD });

  const isPGSupported = isVersionPGSupported(dbVersion);

  useUpdateEffect(() => {
    if (pgValue) setAnalyzeModal(true);
  }, [pgValue]);

  return (
    <Box
      sx={{
        display: 'flex',
        width: '548px',
        backgroundColor: '#FBFCFD',
        border: '1px solid #D7DEE4',
        borderRadius: '8px',
        padding: '16px 24px'
      }}
      data-testid="PGCompatibiltyField-Container"
    >
      <YBTooltip
        title={
          isPGSupported ? (
            ''
          ) : (
            <StyledSubText>
              <Trans>
                {t('tooltip')}
                <StyledLinkText
                  underline="always"
                  href="https://docs.yugabyte.com/preview/explore/ysql-language-features/postgresql-compatibility/"
                  target="_blank"
                ></StyledLinkText>
              </Trans>
            </StyledSubText>
          )
        }
      >
        <div>
          <YBToggleField
            name={PG_COMPATIBILITY_FIELD}
            inputProps={{
              'data-testid': 'PGCompatibiltyField-Toggle'
            }}
            control={control}
            disabled={disabled || !isPGSupported}
            dataTestId="enable-PG-compatibility-field"
          />
        </div>
      </YBTooltip>
      <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%' }}>
        <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center' }}>
          <Typography variant="body2" sx={{ color: '#0B1117', ml: 1, mr: 1 }}>
            {t('label')}&nbsp;
          </Typography>
          <YBEarlyAccessTag />
        </Box>

        <Box>
          <StyledSubText>
            <Trans>
              {t('pgSubText')}
              <StyledLinkText
                underline="always"
                href="https://docs.yugabyte.com/preview/explore/ysql-language-features/postgresql-compatibility/"
                target="_blank"
              ></StyledLinkText>
            </Trans>
          </StyledSubText>
        </Box>
      </Box>
      <AnalyzeDialog open={openAnalyzeModal} onClose={() => setAnalyzeModal(false)} />
    </Box>
  );
};
