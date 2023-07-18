import { FC } from 'react';
import { Box } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBTextarea } from '../../../redesign/components';
import { CONST_VALUES, verifyLDAPAttributes } from '../../../utils/UniverseUtils';

const CONF_PREFIX = '--ysql_hba_conf_csv=';
interface PreviewConfFormProps {
  formProps?: any;
}

export const PreviewGFlagsConf: FC<PreviewConfFormProps> = ({ formProps }) => {
  const { t } = useTranslation();
  const previewConfValue =
    CONF_PREFIX +
    CONST_VALUES.SINGLE_QUOTES_SEPARATOR +
    formProps?.values?.previewFlagValue +
    CONST_VALUES.SINGLE_QUOTES_SEPARATOR;
  const { isAttributeInvalid, errorMessage, isWarning } = verifyLDAPAttributes(previewConfValue);

  return (
    <Box>
      <YBTextarea
        minRows={9}
        maxRows={15}
        readOnly={true}
        value={previewConfValue}
        error={isAttributeInvalid}
        isWarning={isWarning}
        message={t(errorMessage)}
      />
    </Box>
  );
};
