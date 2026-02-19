import { mui } from '@yugabyte-ui-library/core';

const { Box, styled } = mui;

export const StyledPane = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  gap: theme.spacing(3),
  padding: theme.spacing(3),
  background: '#fff',
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px'
}));

export const StyledContent = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  color: theme.palette.grey[600],
  backgroundColor: theme.palette.common.white,
  borderRadius: '8px',
  padding: '24px',
  gap: '16px'
}));

export const StyledHeader = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'row',
  justifyContent: 'space-between',
  alignItems: 'center',
  '& .header-title': {
    fontSize: '15px',
    fontWeight: 600,
    color: theme.palette.grey[900]
  }
}));
