import { mui } from '@yugabyte-ui-library/core';

const { Box, styled, Typography, Link } = mui;

export const StyledPanel = styled('div')(({ theme }) => ({
  width: '100%',
  padding: '0',
  backgroundColor: '#fff',
  borderRadius: '8px',
  border: `1px solid ${theme.palette.grey[200]}`
}));

export const StyledHeader = styled(Typography)(({ theme }) => ({
  padding: `24px`,
  fontSize: 15,
  color: theme.palette.grey[900],
  fontWeight: 600,
  lineHeight: '16px'
}));

export const StyledContent = styled('div')(({ theme }) => ({
  padding: `8px 24px 24px 24px`,
  display: 'flex',
  gap: theme.spacing(4),
  flexDirection: 'column'
}));

export const StyledLink = styled(Link)(({ theme }) => ({
  color: '#4E5F6D',
  fontSize: 11.5,
  fontWeight: 400,
  lineHeight: '18px',
  textDecoration: 'underline',
  textDecorationStyle: 'solid',
  textUnderlinePosition: 'from-font'
}));

export const FieldContainer = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  width: '548px',
  height: 'auto',
  backgroundColor: '#FBFCFD',
  border: '1px solid #D7DEE4',
  borderRadius: '8px'
}));

export const StyledInfoRow = styled(Box)(({ theme }) => ({
  display: 'flex',
  gap: '40px',
  flexDirection: 'column',
  '& > div': {
    display: 'flex',
    flexDirection: 'column',
    gap: '4px',
    '& > .header': {
      color: theme.palette.grey[600],
      textTransform: 'uppercase',
      fontSize: '11.5px',
      fontWeight: 500
    },
    '& > .value': {
      color: theme.palette.grey[900],
      fontSize: '13px',
      fontWeight: 400,
      '&.sameline': {
        display: 'flex',
        alignItems: 'center',
        gap: '8px',
        '&.nogap': {
          gap: 0
        },
        '&>svg': {
          marginTop: '-8px',
          cursor: 'pointer'
        }
      }
    }
  }
}));
