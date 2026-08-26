import { mui } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';

const { Box, Link, styled } = mui;

const Badge = styled(Box)(({ theme }) => ({
  display: 'inline-flex',
  alignItems: 'center',
  height: '40px',
  padding: '0 16px',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  boxSizing: 'border-box',
  width: 'fit-content'
}));

const BadgeText = styled('span')(({ theme }) => ({
  fontSize: '13px',
  fontWeight: 500,
  lineHeight: '16px',
  color: theme.palette.grey[900]
}));

const Label = styled(BadgeText)({
  paddingRight: '8px'
});

const Pipe = styled('span')(({ theme }) => ({
  display: 'inline-block',
  width: '16px',
  height: '1px',
  margin: '0 8px',
  backgroundColor: theme.palette.grey[300],
  transform: 'rotate(90deg)',
  flexShrink: 0
}));

const EditLink = styled(Link)(({ theme }) => ({
  fontSize: '11.5px',
  fontWeight: 600,
  lineHeight: '16px',
  color: theme.palette.primary[600],
  textDecoration: 'underline',
  cursor: 'pointer',
  border: 'none',
  background: 'none',
  padding: 0,
  fontFamily: 'inherit',
  '&:hover': {
    color: theme.palette.primary[600],
    textDecoration: 'underline'
  }
}));

export type TotalNodesBadgeProps = {
  label: string;
  count: number;
  /** Edit Universe / Edit Read Replica */
  onEdit?: () => void;
  dataTestId?: string;
};

export const TotalNodesBadge = ({ label, count, onEdit, dataTestId }: TotalNodesBadgeProps) => {
  const { t } = useTranslation('translation', { keyPrefix: 'common' });

  return (
    <Badge data-testid={dataTestId}>
      <Label>{label}</Label>
      <BadgeText>{count}</BadgeText>
      {onEdit && (
        <>
          <Pipe aria-hidden />
          <EditLink component="button" type="button" onClick={onEdit} underline="always">
            {t('edit')}
          </EditLink>
        </>
      )}
    </Badge>
  );
};
