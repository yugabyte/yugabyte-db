import { createTheme } from '@material-ui/core';
import { colors, themeVariables as variables } from '@app/theme/variables';

export const mainTheme = createTheme({
  palette: {
    primary: {
      ...colors.primary,
      main: colors.primary[600]
    },
    secondary: {
      ...colors.secondary,
      main: colors.secondary[600]
    },
    grey: {
      ...colors.grey
    },
    error: {
      ...colors.error,
      main: colors.error[500]
    },
    warning: {
      ...colors.warning,
      main: colors.warning[500]
    },
    success: {
      ...colors.success,
      main: colors.success[500]
    },
    info: {
      ...colors.info,
      main: colors.info[500]
    },
    common: {
      ...colors.common
    },
    background: {
      ...colors.background
    },
    text: {
      primary: colors.grey[900],
      secondary: colors.grey[600],
      disabled: colors.grey[600],
      highlighted: colors.common.indigo
    },
    action: {
      hover: colors.primary[100],
      selected: colors.primary[200],
      disabledBackground: colors.grey[300]
    },
    chart: {
      stroke: colors.chartStroke,
      fill: colors.chartFill
    },
    divider: colors.grey[200]
  },
  shape: {
    borderRadius: variables.borderRadius,
    shadowLight: variables.shadowLight,
    shadowThick: variables.shadowThick
  },
  typography: {
    fontSize: 13,
    fontFamily: '"Inter", sans-serif',
    allVariants: {
      letterSpacing: 0,
      lineHeight: 1.25
    },
    h1: {
      fontSize: 32,
      fontWeight: 700
    },
    h2: {
      fontSize: 24,
      fontWeight: 700
    },
    h3: {
      fontSize: 21,
      fontWeight: 700
    },
    h4: {
      fontSize: 18,
      fontWeight: 700
    },
    h5: {
      fontSize: 15,
      fontWeight: 700
    },
    body1: {
      fontSize: 13,
      fontWeight: 600
    },
    body2: {
      fontSize: 13,
      fontWeight: 400
    },
    subtitle1: {
      fontSize: 11.5,
      fontWeight: 400
    },
    subtitle2: {
      fontSize: 11.5,
      fontWeight: 600
    },
    button: {
      fontSize: 11.5,
      fontWeight: 500,
      textTransform: 'uppercase'
    },
    caption: {
      fontSize: 10,
      fontWeight: 400,
      textTransform: 'uppercase'
    }
  },
  props: {
    MuiAppBar: {
      elevation: 0
    },
    MuiPaper: {
      elevation: 0
    },
    MuiInputLabel: {
      shrink: true // disable "flying" label effect
    },
    MuiInput: {
      disableUnderline: true
    },
    MuiButton: {
      disableElevation: true // globally disable buttons shadow
    },
    MuiButtonBase: {
      disableRipple: true // globally disable ripple effect
    }
  },
  mixins: {
    toolbar: {
      minHeight: variables.toolbarHeight,
      whiteSpace: 'nowrap'
    }
  },
  overrides: {
    MuiAppBar: {
      root: {
        border: 'none',

        '&.MuiPaper-root': {
          border: 'none',
          backgroundColor: 'white'
        }
      }
    },
    MuiLink: {
      root: {
        cursor: 'pointer'
      }
    },
    MuiButton: {
      root: {
        minWidth: 12,
        height: 32
      },
      sizeSmall: {
        height: 24
      },
      sizeLarge: {
        height: 41,
        '& $label': {
          fontSize: 15
        }
      },
      label: {
        fontSize: 13,
        fontWeight: 600,
        textTransform: 'none'
      },
      startIcon: {
        marginRight: 4,
        '&$iconSizeLarge': {
          marginRight: 8
        }
      },
      endIcon: {
        marginLeft: 4,
        '&$iconSizeLarge': {
          marginLeft: 8
        }
      },
      // "primary" variant
      contained: {
        color: colors.common.white,
        backgroundColor: colors.primary[600],
        '&:hover': {
          backgroundColor: colors.primary[500]
        },
        '&:active': {
          backgroundColor: colors.primary[700]
        },
        '&$disabled': {
          backgroundColor: colors.grey[300],
          color: colors.grey[600]
        }
      },
      // "secondary" variant
      outlined: {
        color: colors.primary[600],
        backgroundColor: colors.background.paper,
        border: `1px solid ${colors.primary[600]}`,
        '&:hover': {
          backgroundColor: colors.background.paper,
          borderColor: colors.primary[500]
        },
        '&:active': {
          backgroundColor: colors.background.paper,
          borderColor: colors.primary[700]
        },
        '&$disabled': {
          backgroundColor: colors.grey[300],
          color: colors.grey[600]
        }
      },
      // "ghost" variant
      text: {
        color: colors.primary[600],
        backgroundColor: 'transparent',
        border: '1px solid transparent',
        '&:hover': {
          backgroundColor: 'transparent',
          border: `1px solid ${colors.primary[500]}`
        },
        '&:active': {
          backgroundColor: 'transparent',
          border: `1px solid ${colors.primary[700]}`
        },
        '&$disabled': {
          backgroundColor: 'transparent',
          color: colors.grey[600]
        }
      }
    },
    MuiAccordion: {
      root: {
        display: 'flex',
        flexDirection: 'column',
        width: '100%'
      }
    },
    MuiAutocomplete: {
      icon: {
        color: colors.grey[600],
        right: 2
      },
      input: {
        border: 0,
        boxShadow: 'none',
        '&:focus': {
          outline: 'none'
        },
        marginLeft: 3
      },
      inputRoot: {
        minHeight: 24,
        height: 'auto',
        padding: 4
      },
      tag: {
        backgroundColor: colors.primary[200],
        borderRadius: 6,
        '&:hover': {
          backgroundColor: colors.primary[100]
        },
        '& .MuiChip-deleteIcon': {
          width: 16,
          height: 16
        },
        fontSize: 11.5,
        lineHeight: 16,
        fontWeight: 400,
        height: 24,
        margin: 0,
        marginRight: 4,
        marginBottom: 4,
        border: 0
      },
      option: {
        fontSize: 13,
        fontWeight: 400,
        paddingTop: 6,
        paddingBottom: 6,
        minHeight: 32,
        lineHeight: 1.25,
        paddingLeft: '16px !important'
      },
      groupLabel: {
        fontSize: 13,
        fontWeight: 400,
        paddingTop: 6,
        paddingBottom: 6,
        minHeight: 32,
        lineHeight: 1.25,
        paddingLeft: '16px !important'
      }
    },
    MuiInput: {
      root: {
        overflow: 'hidden',
        height: variables.inputHeight,
        color: colors.grey[900],
        backgroundColor: colors.background.paper,
        borderRadius: variables.borderRadius,
        border: `1px solid ${colors.grey[300]}`,
        '&:hover': {
          borderColor: colors.primary[300]
        },
        '&$focused': {
          borderColor: colors.primary[300],
          boxShadow: `0 0 0 2px ${colors.primary[200]}`
        },
        '&$error': {
          color: colors.error[500],
          backgroundColor: colors.error[100],
          borderColor: colors.error[500],
          '&:hover': {
            borderColor: colors.error[500]
          },
          '&$focused': {
            boxShadow: `0 0 0 2px ${colors.error[100]}`
          },
          '&$disabled': {
            borderColor: colors.grey[300]
          }
        },
        '&$disabled': {
          color: colors.grey[600],
          backgroundColor: colors.grey[200],
          borderColor: colors.grey[300]
        },
        '&$multiline': {
          height: 'auto',
          padding: 0
        }
      },
      input: {
        padding: 8,
        fontWeight: 400,
        fontSize: 13,
        '&::placeholder': {
          color: colors.grey[600]
        }
      },
      // remove gap between label and input
      formControl: {
        'label + &': {
          marginTop: 0
        }
      }
    },
    MuiChip: {
      root: {
        borderRadius: 4,
        fontSize: 10
      }
    },
    MuiPopover: {
      paper: {
        marginTop: 1,
        fontSize: 13,
        lineHeight: '32px',
        borderWidth: 1,
        borderColor: colors.grey[300],
        borderStyle: 'solid',
        borderRadius: variables.borderRadius,
        boxShadow: 'none'
      }
    },
    MuiInputLabel: {
      root: {
        display: 'flex',
        alignItems: 'center',
        color: colors.grey[600],
        fontSize: 11.5,
        lineHeight: '16px',
        fontWeight: 500,
        textTransform: 'uppercase',
        '&$focused': {
          color: colors.grey[600]
        },
        '&$error': {
          color: colors.error[500]
        },
        '&$disabled': {
          color: colors.grey[600]
        }
      },
      formControl: {
        position: 'relative',
        marginBottom: 4
      },
      shrink: {
        transform: 'translate(0, 1.5px)'
      }
    },
    MuiFormHelperText: {
      root: {
        fontSize: 11.5,
        lineHeight: '16px',
        fontWeight: 400,
        textTransform: 'none',
        color: colors.grey[600],
        marginTop: 8,
        '&$error': {
          '&$disabled': {
            color: colors.grey[600]
          }
        }
      }
    },
    MuiSelect: {
      select: {
        display: 'flex',
        alignItems: 'center',
        '&&': {
          paddingRight: 28
        },
        '&:focus': {
          backgroundColor: 'inherit'
        },
        '&$disabled': {
          cursor: 'not-allowed'
        }
      },
      icon: {
        color: colors.grey[600],
        right: 2
      }
    },
    MuiPaper: {
      root: {
        border: `1px solid ${colors.grey[300]}`
      }
    },
    MuiMenu: {
      paper: {
        boxShadow: variables.shadowThick
      }
    },
    MuiMenuItem: {
      root: {
        height: 32,
        fontSize: 13,
        fontWeight: 400,
        color: colors.grey[900]
      }
    },
    MuiListItem: {
      root: {
        '&$selected': {
          backgroundColor: colors.primary[200],
          '&:hover': {
            backgroundColor: colors.primary[200]
          },
          '&:focus': {
            backgroundColor: colors.primary[100]
          }
        },
        '&.Mui-focusVisible': {
          backgroundColor: 'unset'
        }
      },
      button: {
        '&:hover': {
          backgroundColor: colors.primary[100]
        },
        '&$selected': {
          backgroundColor: colors.primary[200]
        },
        '&:focus': {
          backgroundColor: colors.primary[100]
        }
      }
    },
    MuiToolbar: {
      gutters: {
        ['@media (min-width:600px)']: {
          paddingLeft: 16,
          paddingRight: 16
        }
      }
    },
    MuiContainer: {
      root: {
        ['@media (min-width:600px)']: {
          paddingLeft: 16,
          paddingRight: 16
        }
      }
    },
    MuiFormControlLabel: {
      root: {
        marginLeft: 0,

        '&$disabled': {
          cursor: 'not-allowed'
        }
      }
    },
    MuiDrawer: {
      paperAnchorDockedLeft: {
        border: 'none',
        boxShadow: `inset -1px 0 0 0 ${colors.grey[200]}`
      }
    },
    MuiTab: {
      root: {
        fontSize: 13,
        fontWeight: 400,
        textTransform: 'none',
        minHeight: '56px',

        '&:hover': {
          boxShadow: `inset 0px -3px 0px 0px ${colors.secondary[400]}`
        },

        ['@media (min-width:600px)']: {
          minWidth: 'fit-content',
          marginRight: 32,
          paddingRight: 0,
          paddingLeft: 0
        }
      },
      textColorPrimary: {
        color: colors.grey[900],
        '&$selected': {
          color: colors.grey[900]
        }
      }
    },
    MuiTabs: {
      indicator: {
        background: colors.secondary[900],
        height: 3,
        '&:hover': {
          backgroundColor: colors.secondary[400]
        }
      },
      fixed: {
        boxShadow: `inset 0 -1px ${colors.grey[300] ?? ''}`
      }
    },
    MuiDialog: {
      paperWidthSm: {
        width: 608
      },
      paperWidthMd: {
        width: 800
      }
    },
    MuiDialogContent: {
      root: {
        padding: '8px 16px',
        overflow: 'hidden'
      }
    },
    MuiDialogActions: {
      root: {
        background: colors.grey[200],
        padding: '16px'
      }
    },
    MuiTooltip: {
      tooltip: {
        maxWidth: 360,
        backgroundColor: colors.background.paper,
        color: colors.grey[900],
        border: `1px solid ${colors.grey[300]}`,
        padding: '12px 16px',
        fontSize: 13,
        fontWeight: 400,
        boxShadow: variables.shadowThick
      },
      arrow: {
        '&:before': {
          color: colors.background.paper,
          border: `1px solid ${colors.grey[300]}`
        }
      },
      tooltipPlacementTop: {
        ['@media (min-width: 600px)']: {
          margin: '8px -2px'
        }
      }
    },
    MuiSnackbar: {
      anchorOriginBottomRight: {
        ['@media (min-width: 600px)']: {
          bottom: 56,
          right: 16
        }
      }
    },
    MuiPickersBasePicker: {
      pickerView: {
        justifyContent: 'start',
        minWidth: 280,
        minHeight: 280
      }
    },
    MuiPickersCalendarHeader: {
      switchHeader: {
        backgroundColor: colors.grey[100],
        marginTop: 0,
        '& .MuiPickersCalendarHeader-iconButton': {
          padding: '6px 12px',
          backgroundColor: colors.grey[100]
        },
        '& .MuiPickersCalendarHeader-transitionContainer': {
          height: 19
        }
      }
    },
    MuiPickersCalendar: {
      week: {
        '& .MuiTypography-body2': {
          fontSize: 11,
          fontWeight: 500,
          color: '#333'
        }
      }
    },
    MuiPickersDay: {
      day: {
        fontWeight: 300,
        fontSize: 10,
        borderRadius: 5,
        '&:hover': {
          backgroundColor: colors.primary[200]
        }
      },
      daySelected: {
        backgroundColor: colors.primary[200],
        borderRadius: 5,
        fontWeight: 600,
        '&:hover': {
          backgroundColor: colors.primary[200]
        }
      },
      dayDisabled: {
        color: `${colors.grey[100]} !important`,
        '& .MuiTypography-body2': {
          color: `${colors.grey[400]} !important`
        }
      },
      current: {}
    },
    MuiPickersModal: {
      dialogAction: {
        color: colors.primary[400]
      }
    },
    MuiBadge: {
      badge: {
        borderRadius: 4,
        fontSize: 9,
        padding: 0,
        height: 14,
        minWidth: 16,
        fontWeight: 600
      },
      anchorOriginTopRightRectangle: {
        transform: 'scale(1) translate(70%, -50%)'
      }
    },
    MuiTableContainer: {
      root: {
        boxShadow: 'none',
        // padding: '3px 10px 10px 10px',
        padding: '3px 16px 10px 16px',
        border: `1px solid ${colors.grey[300]}`,
        '& .MuiPaper-root': {
          border: 0
        }
      }
    },
    MuiTableHead: {
      root: {
        '& .MuiIconButton-root': {
          padding: '0 4px'
        },
        '& .MuiCheckbox-root': {
          color: colors.primary[600]
        }
      }
    },
    MuiTableBody: {
      root: {
        '& .MuiTableRow-root': {
          '&:last-child td': {
            borderBottom: 0
          },
          '& .MuiIconButton-root': {
            padding: '0 4px'
          },
          '& .MuiSwitch-switchBase': {
            padding: '4px'
          }
        }
      }
    },
    MuiTableRow: {
      root: {
        '& .actionIcon': {
          display: 'none'
        },
        '&:hover .actionIcon': {
          display: 'block'
        }
      },
      hover: {
        cursor: 'pointer'
      }
    },
    MuiTablePagination: {
      input: {
        fontWeight: 400
      }
    },
    MuiTableCell: {
      root: {
        fontWeight: 400,
        lineHeight: 1.43,
        borderBottom: `1px solid ${colors.grey[300]}`,
        '& .PrivateSwitchBase-root-31': {
          padding: '4px 8px'
        },
        '& .MuiTableSortLabel-root': {
          maxHeight: 24
        }
      },
      sizeSmall: {
        padding: '6px 0px',
        // padding: '0px 16px',
        // lineHeight: '32px'
      },
      head: {
        fontSize: 11.5,
        fontWeight: 600,
        padding: '8px 0',
        borderBottom: `1px solid ${colors.grey[300]}`
      }
    },
    MUIDataTableToolbarSelect: {
      root: {
        display: 'none'
      }
    },
    MUIDataTableToolbar: {
      root: {
        display: 'none'
      }
    },
    MuiTableFooter: {
      root: {
        '& .MuiTableCell-root': {
          border: 0
        }
      }
    },
    MUIDataTableBody: {
      emptyTitle: {
        padding: '16px 0',
        fontWeight: 400
      }
    }
  }
});
