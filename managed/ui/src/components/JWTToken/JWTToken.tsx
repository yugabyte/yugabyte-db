import { FC } from 'react';
import { Box, Theme, Typography, makeStyles } from '@material-ui/core';
import Cookies from 'js-cookie';
import { useTranslation } from 'react-i18next';
import { YBLabel, YBTextarea } from '../../redesign/components';
import { YBCopyButton } from '../common/descriptors';
import YBLogoWithText from '../common/YBLogo/images/yb_yblogo_text.svg';
import { isEmptyString, isNonEmptyString } from '../../utils/ObjectUtils';
import { formatDatetime } from '../../redesign/helpers/DateUtils';

const useStyles = makeStyles((theme: Theme) => ({
  oidcJWTInfo: {
    display: 'flex',
    flexDirection: 'column',
    padding: theme.spacing(15)
  },
  tokenExpiryInfo: {
    display: 'flex',
    justifyContent: 'flex-end'
  },
  tokenExpiryText: {
    fontFamily: 'Inter',
    fontWeight: 600,
    fontSize: '13px',
    color: '#67666C'
  },
  ysqlCommand: {
    width: '97%',
    minHeight: '200px',
    maxHeight: '248px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: '8px',
    background: theme.palette.ybacolors.backgroundDisabled,
    overflow: 'hidden',
    overflowWrap: 'anywhere',
    padding: theme.spacing(1.5),
    marginTop: theme.spacing(1)
  },
  ysqlCommandText: {
    fontFamily: 'Andale Mono',
    fontWeight: 400,
    fontSize: '13px'
  },
  redirectLinkText: {
    textDecoration: 'underline'
  }
}));

export const JWTToken: FC<any> = () => {
  const { t } = useTranslation();
  const helperClasses = useStyles();
  const oidcJWTToken = Cookies.get('jwt_token');
  const expiryDate = Cookies.get('expiration');
  const ysqlCommandText = `ysqlsh <username>/${oidcJWTToken}`;
  const localDate = formatDatetime(expiryDate!);

  return (
    <>
      <Box>
        <Typography variant="h2" className="content-title">
          <img src={YBLogoWithText} alt="logo" />
        </Typography>

        <Box className={helperClasses.oidcJWTInfo}>
          {isNonEmptyString(oidcJWTToken) && (
            <>
              <Box display="flex" flexDirection="row" alignItems={'center'}>
                <Box flex={1}>
                  <Box ml={3}>
                    <Typography variant="h3">{t('OIDCJWT.OIDCJWTToken')}</Typography>
                  </Box>
                  <Box ml={2}>
                    <YBTextarea minRows={2} maxRows={20} disabled={true} value={oidcJWTToken} />
                  </Box>
                  <Box className={helperClasses.tokenExpiryInfo}>
                    <YBLabel
                      dataTestId="OIDCJWTExpiryDate-Label"
                      width="144px"
                      className={helperClasses.tokenExpiryText}
                    >
                      {`${t('OIDCJWT.expirationDate')}: `}
                    </YBLabel>
                    <span className={helperClasses.tokenExpiryText}>{localDate}</span>
                  </Box>
                </Box>
                <Box ml={1}>
                  <YBCopyButton text={oidcJWTToken} />
                </Box>
              </Box>

              <Box mt={3} ml={3} display="flex" flexDirection="column">
                <Typography variant="body2">{t('OIDCJWT.ysqlCommandLabel')}</Typography>
                <Box display="flex" flexDirection="row" alignItems={'center'}>
                  <Box className={helperClasses.ysqlCommand}>
                    <span className={helperClasses.ysqlCommandText}>{`$ ${ysqlCommandText}`}</span>
                  </Box>
                  <YBCopyButton text={ysqlCommandText} />
                </Box>
              </Box>
            </>
          )}
          {(isEmptyString(oidcJWTToken) || !oidcJWTToken) && (
            <Box display="flex" flexDirection="column" alignItems={'center'}>
              <Box>
                <Typography variant="h5">{t('OIDCJWT.errorMessage')}</Typography>
              </Box>
            </Box>
          )}
          <Box mt={2} alignSelf={'center'}>
            <a href="/" className={helperClasses.redirectLinkText}>
              {t('OIDCJWT.redirectLinkText')}
            </a>
          </Box>
        </Box>
      </Box>
    </>
  );
};
