import { createTheme } from '@material-ui/core';
import { colors, themeVariables as variables } from './variables';

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
    orange: {
      ...colors.orange
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
    ybacolors: {
      ...colors.ybacolors
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
    h6: {
      fontSize: 14,
      fontWeight: 500
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
          backgroundColor: 'transparent'
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
        fontSize: 14,
        fontWeight: 300,
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
        backgroundColor: colors.orange[500],
        '&:hover': {
          backgroundColor: colors.orange[700]
        },
        '&:active': {
          backgroundColor: colors.orange[900]
        },
        '&$disabled': {
          backgroundColor: colors.grey[300],
          color: colors.grey[600],
          cursor: 'not-allowed',
          pointerEvents: 'unset',
          '&:hover': {
            backgroundColor: colors.grey[300],
            color: colors.grey[600]
          }
        }
      },
      // "secondary" variant
      outlined: {
        color: colors.ybacolors.ybDarkGray,
        backgroundColor: colors.common.white,
        border: `1px solid ${colors.ybacolors.ybGray}`,
        '&:hover': {
          backgroundColor: colors.ybacolors.ybBorderGray,
          borderColor: colors.ybacolors.ybBorderGray
        },
        '&:active': {
          backgroundColor: colors.ybacolors.ybDarkGray2,
          borderColor: colors.ybacolors.ybDarkGray2
        },
        '&$disabled': {
          opacity: 0.65,
          color: colors.ybacolors.ybDarkGray,
          cursor: 'not-allowed',
          pointerEvents: 'unset',
          '&:hover': {
            opacity: 0.65,
            color: colors.ybacolors.ybDarkGray
          }
        }
      },
      // "ghost" variant
      text: {
        color: colors.orange[700],
        backgroundColor: 'transparent',
        '&:hover': {
          backgroundColor: 'transparent'
        },
        '&:active': {
          backgroundColor: 'transparent'
        },
        '&$disabled': {
          color: colors.grey[600],
          cursor: 'not-allowed',
          pointerEvents: 'unset',
          '&:hover': {
            color: colors.grey[600]
          }
        }
      }
    },
    MuiToggleButtonGroup: {
      groupedHorizontal: {
        '&:not(:first-child)': {
          borderLeft: `1px solid ${colors.ybacolors.ybBorderGray}`
        }
      }
    },
    MuiToggleButton: {
      root: {
        '&.Mui-selected': {
          backgroundColor: '#e9eef9'
        }
      }
    },
    MuiAccordion: {
      root: {
        display: 'flex',
        flexDirection: 'column',
        width: '100%',
        '& .MuiIconButton-root': {
          color: colors.primary[900]
        }
      }
    },
    MuiCheckbox: {
      root: {
        '& .MuiSvgIcon-root': {
          width: 16,
          height: 16,
          color: colors.primary[600]
        }
      },
      colorPrimary: {
        '&.Mui-checked': {
          color: colors.orange[500]
        }
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
        minHeight: variables.inputHeight,
        height: 'auto !important',
        padding: 4
      },
      tag: {
        backgroundColor: colors.ybacolors.inputBackground,
        borderRadius: 6,
        '&:hover': {
          backgroundColor: colors.ybacolors.inputBackground,
          opacity: 0.8
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
        paddingLeft: '16px !important',
        '&[aria-disabled=true]': {
          pointerEvents: 'none'
        },
        color: '#232329'
      },
      groupLabel: {
        fontSize: 13,
        fontWeight: 400,
        paddingTop: 6,
        paddingBottom: 6,
        minHeight: 32,
        lineHeight: 1.25,
        paddingLeft: '16px !important'
      },
      groupUl: {
        borderBottom: '1px solid #E9EEF2'
      },
      '&$disabled': {
        cursor: 'not-allowed'
      }
    },
    MuiInput: {
      root: {
        overflow: 'hidden',
        height: variables.inputHeight,
        color: colors.grey[900],
        backgroundColor: colors.background.paper,
        borderRadius: variables.borderRadius,
        border: `1px solid ${colors.ybacolors.ybGray}`,
        '&:hover': {
          borderColor: colors.ybacolors.inputBackground
          // borderColor: colors.primary[300]
        },
        '&$focused': {
          borderColor: colors.ybacolors.ybOrangeFocus,
          boxShadow: colors.ybaShadows.inputBoxShadow
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
            borderColor: colors.ybacolors.ybGray
          }
        },
        '&$disabled': {
          color: colors.ybacolors.colorDisabled,
          backgroundColor: colors.ybacolors.backgroundDisabled,
          borderColor: colors.ybacolors.ybGray,
          cursor: 'not-allowed'
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
    MuiInputBase: {
      input: {
        height: 'inherit',
        '&$disabled': {
          cursor: 'not-allowed'
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
        right: 2,
        top: 'auto'
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
        '@media (min-width:600px)': {
          paddingLeft: 16,
          paddingRight: 16
        }
      }
    },
    MuiContainer: {
      root: {
        '@media (min-width:600px)': {
          paddingLeft: 16,
          paddingRight: 16
        }
      }
    },
    MuiFormControl: {
      root: {
        justifyContent: 'center'
      }
    },
    MuiFormControlLabel: {
      root: {
        marginLeft: 0,
        marginBottom: 0,

        '&$disabled': {
          cursor: 'not-allowed',
          opacity: 0.6
        }
      }
    },
    MuiDrawer: {
      paperAnchorDockedLeft: {
        border: 'none',
        boxShadow: `inset -1px 0 0 0 ${colors.grey[200]}`
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
        padding: '16px',
        '& .MuiButtonBase-root': {
          height: 44
        }
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
        '@media (min-width: 600px)': {
          margin: '8px -2px'
        }
      }
    },
    MuiTabs: {
      root: {
        '& .MuiTabs-indicator': {
          backgroundColor: colors.orange[500],
          height: 4
        },
        '& .MuiButtonBase-root': {
          padding: 0,
          textTransform: 'none',
          fontSize: 14,
          fontWeight: 500,
          color: colors.ybacolors.labelBackground
        },
        borderBottom: `1px solid ${colors.ybacolors.ybBorderGray}`
      }
    },
    MuiSnackbar: {
      anchorOriginBottomRight: {
        '@media (min-width: 600px)': {
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
        padding: '3px 10px 10px 10px',
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
        padding: 0,
        lineHeight: '50px'
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
