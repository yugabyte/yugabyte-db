import { FC, useState } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation, Trans } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { mui, YBToggleField, YBTooltip } from '@yugabyte-ui-library/core';
import { FieldContainer } from '../../components/DefaultComponents';
import { YBEarlyAccessTag } from '../../../../../components';
import { AnalyzeDialog } from '../../../../../features/universe/universe-actions/edit-pg-compatibility/AnalyzeDialog';
import { isVersionPGSupported } from '../../../../../features/universe/universe-form/utils/helpers';
import { DatabaseSettingsProps } from '../../steps/database-settings/dtos';

//icons
import InfoIcon from '../../../../../assets/info-new.svg';

const { Box, Typography, Link, styled } = mui;

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
    <FieldContainer>
      <Box
        sx={{
          display: 'flex',
          padding: '16px 24px',
          flexDirection: 'column',
          gap: '4px'
        }}
        data-testid="PGCompatibiltyField-Container"
      >
        <Box
          sx={{
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'center',
            gap: '8px'
          }}
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
            <div style={{ marginBottom: '-5px' }}>
              <YBToggleField
                name={PG_COMPATIBILITY_FIELD}
                inputProps={{
                  'data-testid': 'PGCompatibiltyField-Toggle'
                }}
                control={control}
                disabled={disabled || !isPGSupported}
                dataTestId="enable-PG-compatibility-field"
                label={t('label')}
              />
            </div>
          </YBTooltip>
          <InfoIcon />
          <YBEarlyAccessTag />
        </Box>
        <Box sx={{ ml: 5 }}>
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
        <AnalyzeDialog open={openAnalyzeModal} onClose={() => setAnalyzeModal(false)} />
      </Box>
    </FieldContainer>
  );
};
