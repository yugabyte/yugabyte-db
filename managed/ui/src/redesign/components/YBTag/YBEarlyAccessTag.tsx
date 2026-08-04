import { Box, makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

const useTagStyles = makeStyles(() => ({
  eaTag: {
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
  }
}));

export const YBEarlyAccessTag = () => {
  const classes = useTagStyles();
  const { t } = useTranslation();
  return <Box className={classes.eaTag}>{t('universeForm.advancedConfig.earlyAccess')}</Box>;
};
