import { FC } from 'react';
import { Box } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBTextarea } from '../../../redesign/components';
import { CONST_VALUES } from '../../../utils/UniverseUtils';
import { isNonEmptyString } from '../../../utils/ObjectUtils';

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

  const rows =
    serverType === 'TSERVER'
      ? formProps?.values?.tserverFlagDetails?.flagvalueobject
      : formProps?.values?.masterFlagDetails?.flagvalueobject;

  const filteredErrorRow = rows?.find((row: any) => isNonEmptyString(row?.errorMessageKey));
  const errorMessageKey = filteredErrorRow?.errorMessageKey;
  const isWarning = errorMessageKey === 'universeForm.gFlags.nonASCIIDetected';

  const previewConfValue =
    CONF_PREFIX +
    CONST_VALUES.SINGLE_QUOTES_SEPARATOR +
    flagValue +
    CONST_VALUES.SINGLE_QUOTES_SEPARATOR;

  return (
    <Box>
      <YBTextarea
        minRows={8}
        maxRows={11}
        readOnly={true}
        value={previewConfValue}
        error={isNonEmptyString(errorMessageKey) && !isWarning}
        isWarning={isWarning}
        message={t(errorMessageKey)}
      />
    </Box>
  );
};
