import React, { FC } from 'react';
import { YBLoadingBox } from '@app/components/YBLoadingBox/YBLoadingBox';
import { makeStyles, Box, Theme, Typography, TypographyVariant } from '@material-ui/core';

const useStyles = makeStyles((theme: Theme) => ({
  loadingSection: {
    padding: theme.spacing(2, 0)
  }
}));

interface YBAddSectionProps {
  title?: string;
  body: string;
  bodyVariant?: TypographyVariant;
  controls?: React.ReactNode | React.ReactNodeArray;
}

export const YBAddSection: FC<YBAddSectionProps> = ({ title, body, bodyVariant, controls }) => {
  const classes = useStyles();
  return (
    <div className={classes.loadingSection}>
      <YBLoadingBox>
        {title && (
          <Box mt={1} mb={2}>
            {/* Wrapping in Box because Typography enforces margin: 0 */}
            <Typography variant="h2">{title}</Typography>
          </Box>
        )}
        <Box mb={2}>
          <Typography variant={bodyVariant ?? 'body2'}>{body}</Typography>
        </Box>
        {controls}
      </YBLoadingBox>
    </div>
  );
};
