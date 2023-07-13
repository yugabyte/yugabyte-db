import { MuiPickersOverrides } from '@material-ui-pickers/typings/overrides';
import { QueryKey } from 'react-query/types/core/types';
import { colors } from '../theme/variables';

type overridesNameToClassKey = {
  [P in keyof MuiPickersOverrides]: keyof MuiPickersOverrides[P];
};

declare global {
  // react-query uses an attached cancel() method for canceling queries, thus have to extend Promise type definition
  interface Promise<T> {
    cancel?: () => void;
  }
}

//  Material UI theme customizations
declare module '@material-ui/core/styles/createMuiTheme' {
  interface YBThemeVariables {
    screenMinWidth: number;
    screenMinHeight: number;
    sidebarWidthMin: number;
    sidebarWidthMax: number;
    toolbarHeight: number;
    footerHeight: number;
    inputHeight: number;
    borderRadius: number;
    shadowLight: string;
    shadowThick: string;
  }

  interface Theme {
    variables: YBThemeVariables;
  }

  interface ThemeOptions {
    variables: YBThemeVariables;
  }
}

declare module '@material-ui/core/styles/createPalette' {
  // extend with extra common colors
  interface CommonColors {
    blue: string;
    magenta: string;
    purple: string;
    cyan: string;
    orange: string;
    yellow: string;
    indigo: string;
  }

  // add more text color options
  interface TypeText {
    highlighted: string;
  }

  interface ChartColors {
    stroke: {
      cat1: string;
      cat2: string;
      cat3: string;
      cat4: string;
      cat5: string;
      cat6: string;
      cat7: string;
      cat8: string;
      cat9: string;
      cat10: string;
    };
    fill: {
      area1: string;
      area2: string;
      area3: string;
      area4: string;
      area5: string;
      area6: string;
      area7: string;
      area8: string;
      area9: string;
      area10: string;
    };
  }

  // extend standard palette with chart colors
  interface Palette {
    chart: ChartColors;
    orange: PaletteColor;
    ybacolors: typeof colors.ybacolors;
  }

  interface PaletteOptions {
    chart?: ChartColors;
    orange: PaletteColor;
    ybacolors: YBAColors;
  }

  // extend standard palette with color tones
  interface PaletteColor {
    100: string;
    200: string;
    300: string;
    400: string;
    500: string;
    600: string;
    700: string;
    800: string;
    900: string;
  }
}

declare module '@material-ui/styles/defaultTheme' {
  interface DefaultTheme {
    palette: Palette;
    shape: Shape;
    breakpoints: Breakpoints;
    direction: Direction;
    overrides?: Overrides;
    shadows: Shadows;
    spacing: Spacing;
  }
}

declare module '@material-ui/core/styles/shape' {
  // put our custom shadows into shape to not mix with internal MUI shadows
  interface Shape {
    shadowThick: string;
    shadowLight: string;
  }
}

declare module '@material-ui/core/styles/overrides' {
  export interface ComponentNameToClassKey extends overridesNameToClassKey {}
}
