import { FC, useState } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation, Trans } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import { useFormContext, useWatch } from 'react-hook-form';
import {
  YBInputFieldProps,
  yba,
  mui,
  YBToggleField,
  YBTooltip,
  YBLabel
} from '@yugabyte-ui-library/core';
import { YBEarlyAccessTag } from '../../../../components';
// import { AnalyzeDialog } from '../../../../universe-actions/edit-pg-compatibility/AnalyzeDialog';
// import { isVersionPGSupported } from '../../../utils/helpers';

import { DatabaseSettingsProps } from '../../steps/database-settings/dtos';

const { Box, Typography, Link } = mui;

interface PGCompatibiltyFieldProps {
  disabled: boolean;
}

const PG_COMPATIBILITY_FIELD = 'enablePGCompatibitilty';

const useStyles = makeStyles((theme) => ({
  pill: {
    height: '20px',
    width: 'fit-content',
    padding: '2px 6px',
    border: '1px solid #D7DEE4',
    borderRadius: '4px',
    fontWeight: 600,
    fontSize: '11.5px',
    background: `linear-gradient(to left,#ED35EC, #ED35C5,#7879F1,#5E60F0)`,
    backgroundClip: 'text',
    color: 'transparent'
  },
  subText: {
    fontSize: '11.5px',
    lineHeight: '16px',
    fontWeight: 400,
    color: '#67666C'
  },
  linkText: {
    fontSize: '11.5px',
    lineHeight: '16px',
    fontWeight: 400,
    color: '#67666C'
  }
}));

export const PGCompatibiltyField: FC<PGCompatibiltyFieldProps> = ({ disabled }) => {
  const { control, setValue } = useFormContext<DatabaseSettingsProps>();
  const [openAnalyzeModal, setAnalyzeModal] = useState(false);
  const classes = useStyles();
  const { t } = useTranslation();

  //watchers
  //   const dbVersionValue = useWatch({ name: SOFTWARE_VERSION_FIELD });
  const pgValue = useWatch({ name: PG_COMPATIBILITY_FIELD });

  //   const isPGSupported = isVersionPGSupported(dbVersionValue);
  const isPGSupported = true;

  //   useUpdateEffect(() => {
  //     //set toggle to false if unsupported db version is selected
  //     if (!isVersionPGSupported(dbVersionValue)) setValue(PG_COMPATIBILITY_FIELD, false);
  //   }, [dbVersionValue]);

  //   useUpdateEffect(() => {
  //     if (pgValue) setAnalyzeModal(true);
  //   }, [pgValue]);

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
            <Typography className={classes.subText}>
              <Trans>
                {t('universeForm.advancedConfig.pgTooltip')}
                <Link
                  underline="always"
                  href="https://docs.yugabyte.com/preview/explore/ysql-language-features/postgresql-compatibility/"
                  className={classes.linkText}
                  target="_blank"
                ></Link>
              </Trans>
            </Typography>
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
          />
        </div>
      </YBTooltip>
      <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%' }}>
        <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center' }}>
          <Typography variant="body2" sx={{ color: '#0B1117', ml: 1, mr: 1 }}>
            {t('universeForm.advancedConfig.enhancePGCompatibility')}&nbsp;
          </Typography>
          <YBEarlyAccessTag />
        </Box>

        <Box>
          <Typography className={classes.subText}>
            <Trans>
              {t('universeForm.advancedConfig.pgSubText')}
              <Link
                underline="always"
                href="https://docs.yugabyte.com/preview/explore/ysql-language-features/postgresql-compatibility/"
                className={classes.linkText}
                target="_blank"
              ></Link>
            </Trans>
          </Typography>
        </Box>
      </Box>
      {/* <AnalyzeDialog open={openAnalyzeModal} onClose={() => setAnalyzeModal(false)} /> */}
    </Box>
  );
};
