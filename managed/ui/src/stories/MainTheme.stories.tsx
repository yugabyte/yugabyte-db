import _ from 'lodash';
import type { Meta, StoryObj } from '@storybook/react';
import {
  Box,
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableRow,
  Typography,
  useTheme
} from '@material-ui/core';
import type { Variant } from '@material-ui/core/styles/createTypography';

export default {
  title: 'Theme'
} as Meta;

// map style guide typography variants to material ui
const typographyMap: Record<string, Variant> = {
  H1: 'h1',
  H2: 'h2',
  H3: 'h3',
  H4: 'h4',
  H5: 'h5',
  Default: 'body2',
  'Default, Bold': 'body1',
  Small: 'subtitle1',
  'Small, Bold': 'subtitle2',
  Label: 'button',
  Tag: 'caption'
};

export const ThemeTypographyStory: StoryObj = () => (
  <Table>
    <TableHead>
      <TableRow>
        <TableCell>Style Guide Name</TableCell>
        <TableCell>Material UI Name</TableCell>
        <TableCell>Sample</TableCell>
      </TableRow>
    </TableHead>
    <TableBody>
      {Object.entries(typographyMap).map(([designName, muiVariant]) => (
        <TableRow key={designName}>
          <TableCell>{designName}</TableCell>
          <TableCell>{muiVariant}</TableCell>
          <TableCell>
            <Typography variant={muiVariant}>How quickly daft jumping zebras vex.</Typography>
          </TableCell>
        </TableRow>
      ))}
    </TableBody>
  </Table>
);
ThemeTypographyStory.storyName = 'Typography';

const paletteMap = [
  'ybacolors',
  'orange',
  'chart.stroke',
  'chart.fill',
  'text',
  'primary',
  'secondary',
  'grey',
  'error',
  'warning',
  'success',
  'info',
  'common',
  'background',
  'text'
];

export const ThemePaletteStory: StoryObj = () => {
  const theme = useTheme();

  return (
    <Table>
      <TableBody>
        {paletteMap.map((name) => (
          <TableRow key={name}>
            <TableCell>{name}</TableCell>
            {Object.entries(_.get(theme.palette, name)).map(([color, value]) => (
              <TableCell key={color}>
                <Box
                  width={145}
                  height={60}
                  bgcolor={value}
                  overflow="hidden"
                  display="flex"
                  alignItems="center"
                  justifyContent="center"
                >
                  {color}
                </Box>
                {value}
              </TableCell>
            ))}
          </TableRow>
        ))}
        <TableRow>
          <TableCell>action</TableCell>
          <TableCell>
            <Box
              width={120}
              height={60}
              bgcolor={theme.palette.action.hover}
              overflow="hidden"
              display="flex"
              alignItems="center"
              justifyContent="center"
            >
              hover
            </Box>
            {theme.palette.action.hover}
          </TableCell>
          <TableCell>
            <Box
              width={120}
              height={60}
              bgcolor={theme.palette.action.selected}
              overflow="hidden"
              display="flex"
              alignItems="center"
              justifyContent="center"
            >
              selected
            </Box>
            {theme.palette.action.selected}
          </TableCell>
          <TableCell>
            <Box
              width={120}
              height={60}
              bgcolor={theme.palette.action.disabledBackground}
              overflow="hidden"
              display="flex"
              alignItems="center"
              justifyContent="center"
            >
              disabledBackground
            </Box>
            {theme.palette.action.disabledBackground}
          </TableCell>
        </TableRow>
        <TableRow>
          <TableCell>divider</TableCell>
          <TableCell>
            <Box
              width={120}
              height={60}
              bgcolor={theme.palette.divider}
              overflow="hidden"
              display="flex"
              alignItems="center"
              justifyContent="center"
            >
              divider
            </Box>
            {theme.palette.divider}
          </TableCell>
        </TableRow>
      </TableBody>
    </Table>
  );
};
ThemePaletteStory.storyName = 'Palette';
