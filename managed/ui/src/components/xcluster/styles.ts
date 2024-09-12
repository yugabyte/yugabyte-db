import { makeStyles } from '@material-ui/core';
import { INPUT_FIELD_WIDTH_PX } from './constants';

export const useModalStyles = makeStyles((theme) => ({
  stepContainer: {
    '& ol': {
      paddingLeft: theme.spacing(2),
      listStylePosition: 'outside',
      '& li::marker': {
        fontWeight: 'bold'
      }
    }
  },
  instruction: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    marginBottom: theme.spacing(4)
  },
  formSectionDescription: {
    marginBottom: theme.spacing(3)
  },
  inputField: {
    width: INPUT_FIELD_WIDTH_PX
  },
  fieldLabel: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',

    marginBottom: theme.spacing(1)
  },
  fieldHelpText: {
    marginTop: theme.spacing(1),

    color: theme.palette.ybacolors.textGray,
    fontSize: '12px'
  },
  infoIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  }
}));
