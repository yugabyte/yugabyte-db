import { makeStyles } from '@material-ui/core';

export const auditLogStyles = makeStyles((theme) => ({
  mainTitle: {
    fontWeight: 700,
    fontSize: 22,
    color: '#151838'
  },
  emptyContainer: {
    display: 'flex',
    height: '432px',
    width: '100%',
    borderRadius: '8px',
    backgroundColor: '#FFFFFF',
    border: '1px solid #E5E5E9',
    padding: theme.spacing(3)
  },
  innerEmptyContainer: {
    height: '100%',
    width: '100%',
    display: 'flex',
    flexDirection: 'column',
    padding: theme.spacing(8, 2, 2, 2),
    backgroundColor: 'rgb(229 229 233 / 20%)',
    borderRadius: '8px',
    border: '1px dotted #C8C7CE',
    alignItems: 'center',
    justifyContent: 'space-between'
  },
  auditLogCard: {
    display: 'flex',
    flexDirection: 'column',
    minHeight: '486px',
    width: '100%',
    padding: theme.spacing(3),
    backgroundColor: '#FFFFFF',
    border: '1px solid #E5E5E9',
    borderRadius: '8px',
    rowGap: '4px'
  },
  cardHeader: {
    color: '#151838',
    fontWeight: 700,
    fontSize: 16,
    lineHeight: '20px'
  },
  divider: {
    backgroundColor: '#E9EEF2',
    height: '1px',
    width: '100%'
  },
  cardSubtitle: {
    color: '#6D7C88',
    fontWeight: 500,
    fontSize: 11.5,
    lineHeight: '14px'
  },
  successStatus: {
    color: '#289B42',
    fontWeight: 600,
    fontSize: '16px',
    lineHeight: '20px'
  },
  inProgresText: {
    color: '#495589',
    fontWeight: 600,
    fontSize: 16
  },
  emptyExportContainer: {
    height: '228px',
    width: '100%',
    display: 'flex',
    flexDirection: 'column',
    border: '1px dotted #C8C7CE',
    borderRadius: '8px',
    alignItems: 'center',
    padding: theme.spacing(4, 2, 2, 2),
    backgroundColor: 'rgb(229 229 233 / 20%)',
    rowGap: '4px'
  },
  modalTitle: {
    fontWeight: 600,
    fontSize: 16,
    color: '#000000',
    display: 'block'
  },
  auditLogLink: {
    color: '#3E66FB',
    '&:hover': {
      backgroundColor: '#3E66FB',
      color: '#151730'
    }
  },
  optionContainer: {
    width: '100%',
    display: 'flex',
    flexDirection: 'column',
    border: '1px solid #E5E5E9',
    borderRadius: '8px'
  },
  logOption: {
    display: 'flex',
    flexDirection: 'row',
    padding: theme.spacing(2, 3),
    alignItems: 'center',
    justifyContent: 'space-between',
    borderTop: '1px solid #E5E5E9'
  },
  logOptionWithoutBorder: {
    display: 'flex',
    flexDirection: 'row',
    padding: theme.spacing(2, 3),
    alignItems: 'center',
    justifyContent: 'space-between'
  },
  auditLogClient: {
    display: 'flex',
    flexDirection: 'column',
    width: '100%',
    padding: theme.spacing(2, 3),
    borderTop: '1px solid #E5E5E9'
  },
  clientToggleC: {
    display: 'flex',
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between'
  },
  exportMenuItem: {
    display: 'flex',
    flexDirection: 'column',
    height: '56px',
    padding: theme.spacing(1, 2),
    gap: '8px',
    alignItems: 'flex-start'
  },
  createExportMenuItem: {
    dispaly: 'flex',
    flexDirection: 'row',
    height: '46px',
    padding: theme.spacing(1.25, 2.5),
    fontSize: 15,
    fontWeight: 400,
    color: '#EF5824',
    alignItems: 'center'
  },
  exportInfo: {
    height: '64px',
    width: '100%',
    display: 'flex',
    flexDirection: 'column',
    padding: theme.spacing(1.5, 2),
    border: '1px solid #00819B',
    borderRadius: '8px',
    backgroundColor: '#D7EFF4',
    justifyContent: 'center'
  },
  exportInfoText: {
    fontSize: 14,
    fontWeight: 400,
    lineHeight: '17px',
    color: '#151730'
  },
  disableLogModalConatiner: {
    display: 'flex',
    flexDirection: 'column',
    height: '100%',
    width: '100%',
    justifyContent: 'space-between',
    padding: theme.spacing(1, 2.5, 4, 2.5)
  },
  editMenuItem: {
    height: '48px',
    padding: theme.spacing(2.5, 2)
  }
}));
