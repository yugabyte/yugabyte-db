import { Fragment, ReactNode } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, YBCheckbox, YBTag, YBTooltip } from '@yugabyte-ui-library/core';

import { formatBytes } from '@app/utils/Formatters';
import InfoSmallIcon from '@app/redesign/assets/approved/info-small.svg';

import { SupportBundleCategoryDef } from '../model/categories';

const { styled, Box, Typography } = mui;

const Row = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  gap: theme.spacing(2),
  padding: theme.spacing(2),
  background: theme.palette.common.white,
  '&:not(:last-of-type)': {
    borderBottom: `1px solid ${theme.palette.grey[200]}`
  }
}));

const SensitiveText = styled('span')(({ theme }) => ({
  fontWeight: 600,
  color: theme.palette.warning[700]
}));

const SensitiveTag = styled('span')({
  display: 'inline-flex',
  verticalAlign: 'middle'
});

/**
 * Lines the checkbox up with the category title rather than the middle of a multi-line label,
 * and stops a long label from squeezing the checkbox, which shifted it sideways row to row.
 */
const CheckboxLabel = styled(Box)({
  '& .yb-MuiFormControlLabel-root': {
    alignItems: 'flex-start'
  },
  '& .yb-MuiCheckbox-root': {
    flexShrink: 0,
    marginTop: 2
  }
});

interface CategoryRowProps {
  category: SupportBundleCategoryDef;
  checked: boolean;
  disabled?: boolean;
  /** Undefined while there is no estimate for this category. */
  sizeBytes?: number;
  onToggle?: (checked: boolean) => void;
  dataTestId: string;
  children?: ReactNode;
}

export const CategoryRow = ({
  category,
  checked,
  disabled,
  sizeBytes,
  onToggle,
  dataTestId,
  children
}: CategoryRowProps) => {
  const { t } = useTranslation('translation', { keyPrefix: 'universeActions.supportBundleV2' });
  const hasSize = checked && sizeBytes !== undefined;
  const sensitiveComponent = category.components.find((def) => def.sensitive);

  // The tag sits inline after the last component, as in the design, so it wraps with the text.
  const description = (
    <Typography component="div" variant="subtitle1" color="grey.700">
      {category.components.map((def, index) => (
        <Fragment key={def.component}>
          {index > 0 && ' · '}
          {def.sensitive ? (
            <SensitiveText>{t(`component.${def.i18nKey}`)}</SensitiveText>
          ) : (
            t(`component.${def.i18nKey}`)
          )}
        </Fragment>
      ))}
      {sensitiveComponent && ' '}
      {sensitiveComponent && (
        <YBTooltip
          title={
            <Typography variant="subtitle1" whiteSpace="pre-line">
              {t(`sensitiveTooltip.${sensitiveComponent.i18nKey}`)}
            </Typography>
          }
        >
          <SensitiveTag data-testid={`${dataTestId}-Sensitive`}>
            <YBTag size="small" color="warning" endIcon={<InfoSmallIcon />}>
              {t('create.sensitive')}
            </YBTag>
          </SensitiveTag>
        </YBTooltip>
      )}
    </Typography>
  );

  return (
    <Row data-testid={dataTestId}>
      <Box display="flex" alignItems="center" gap={2}>
        <CheckboxLabel flex={1} minWidth={0}>
          <YBCheckbox
            checked={checked}
            disabled={disabled}
            onChange={(event) => onToggle?.(event.target.checked)}
            dataTestId={`${dataTestId}-Checkbox`}
            label={
              <Box display="flex" flexDirection="column" gap={0.5}>
                <Typography variant="body1" lineHeight="20px" color="text.primary">
                  {t(`category.${category.id}`)}
                </Typography>
                {description}
              </Box>
            }
          />
        </CheckboxLabel>
        <Box flexShrink={0} data-testid={`${dataTestId}-Size`}>
          <YBTag size="small" variant="light" color={hasSize ? 'primary' : undefined}>
            {hasSize ? formatBytes(sizeBytes) : '-'}
          </YBTag>
        </Box>
      </Box>
      {children}
    </Row>
  );
};
