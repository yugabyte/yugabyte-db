import { styled, Typography, Link } from '@material-ui/core';

export const StyledPanel = styled('div')(({ theme }) => ({
  padding: '0',
  backgroundColor: '#fff',
  borderRadius: '8px',
  border: `1px solid ${theme.palette.grey[200]}`,
  width: '100%'
}));

export const StyledHeader = styled(Typography)(({ theme }) => ({
  padding: `10px ${theme.spacing(3)}px`,
  fontSize: 15,
  color: theme.palette.grey[900]
}));

export const StyledContent = styled('div')(({ theme }) => ({
  padding: `${theme.spacing(1)}px ${theme.spacing(2.5)}px ${theme.spacing(2.5)}px ${theme.spacing(
    2.5
  )}px`,
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
