import { FC } from 'react';
import { Box } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBTextarea } from '../../../redesign/components';
import { CONST_VALUES, verifyLDAPAttributes } from '../../../utils/UniverseUtils';

interface PreviewConfFormProps {
  formProps: any;
  serverType: string;
  flagName: string;
}

export const PreviewGFlagsConf: FC<PreviewConfFormProps> = ({
  formProps,
  serverType,
  flagName
}) => {
  const { t } = useTranslation();
  const CONF_PREFIX = `--${flagName}=`;

  const flagValue =
    serverType === 'TSERVER'
      ? formProps?.values?.tserverFlagDetails?.previewFlagValue ?? formProps?.values?.flagvalue
      : formProps?.values?.masterFlagDetails?.previewFlagValue ?? formProps?.values?.flagvalue;
  const previewConfValue =
    CONF_PREFIX +
    CONST_VALUES.SINGLE_QUOTES_SEPARATOR +
    flagValue +
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
