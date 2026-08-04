import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, BoxProps } from '@material-ui/core';
import { AlertVariant, YBAlert } from '@app/components';

interface GenericFailure extends BoxProps {
  text?: string;
}
export const GenericFailure: FC<GenericFailure> = ({ text, ...props }) => {
  const { t } = useTranslation();
  return (
    <Box padding={4} maxWidth={1024} marginLeft="auto" marginRight="auto" {...props}>
      <YBAlert open text={text ?? t('common.genericFailure')} variant={AlertVariant.Error} />
    </Box>
  );
};
