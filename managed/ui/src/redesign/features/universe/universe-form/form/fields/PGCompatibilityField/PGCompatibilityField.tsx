import { FC } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation, Trans } from 'react-i18next';
import { Box, makeStyles, Typography, Link } from '@material-ui/core';
import { useFormContext, useWatch } from 'react-hook-form';
import { YBToggleField, YBLabel, YBTooltip } from '../../../../../../components';
import { isVersionPGSupported } from '../../../utils/helpers';
import { UniverseFormData } from '../../../utils/dto';
import { PG_COMPATIBILITY_FIELD, SOFTWARE_VERSION_FIELD } from '../../../utils/constants';

interface PGCompatibiltyFieldProps {
  disabled: boolean;
}

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
  const { control, setValue } = useFormContext<UniverseFormData>();
  const classes = useStyles();
  const { t } = useTranslation();

  //watchers
  const dbVersionValue = useWatch({ name: SOFTWARE_VERSION_FIELD });

  const isPGSupported = isVersionPGSupported(dbVersionValue);

  useUpdateEffect(() => {
    //set toggle to false if unsupported db version is selected
    if (!isVersionPGSupported(dbVersionValue)) setValue(PG_COMPATIBILITY_FIELD, false);
  }, [dbVersionValue]);

  return (
    <Box display="flex" width="100%" data-testid="PGCompatibiltyField-Container">
      <YBTooltip
        interactive={true}
        title={
          isPGSupported ? (
            ''
          ) : (
            <Typography className={classes.subText}>
              <Trans>
                {t('universeForm.advancedConfig.pgTooltip')}
                {/* <Link underline="always" className={classes.linkText}></Link> */}
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
      <Box display={'flex'} flexDirection={'column'} width="100%">
        <YBLabel dataTestId="PGCompatibiltyField-Label" width="300px">
          {t('universeForm.advancedConfig.enhancePGCompatibility')}&nbsp;
          <Box className={classes.pill}>{t('universeForm.advancedConfig.earlyAccess')}</Box>
        </YBLabel>
        <Box>
          <Typography className={classes.subText}>
            <Trans>
              {t('universeForm.advancedConfig.pgSubText')}
              {/* <Link underline="always" className={classes.linkText}></Link> */}
            </Trans>
          </Typography>
        </Box>
      </Box>
    </Box>
  );
};
