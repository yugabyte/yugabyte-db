import React, { FC } from "react";
import { Box, makeStyles, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBButton } from "@app/components";
import RefreshIcon from "@app/assets/refresh.svg";

const useStyles = makeStyles(() => {
  const baseSideText = {
    color: 'var(--Text-Color-Grey-700, #4E5F6D)',
    fontFamily: 'Inter',
    fontSize: '11.5px',
    fontStyle: 'normal',
    fontWeight: 400,
    lineHeight: '16px',
    textTransform: 'none' as const
  };
  return {
    heading: {
      color: '#000',
      fontFamily: 'Inter',
      fontSize: '15px',
      fontStyle: 'normal',
      fontWeight: 600,
      lineHeight: '20px'
    },
    hyphen: {
      ...baseSideText,
      margin: '0 10px'
    },
    voyager: {
      ...baseSideText,
      marginRight: 3
    },
    version: {
      ...baseSideText
    }
  };
});

interface HeadingWithVersionProps {
  heading: string;
  voyagerVersion?: string;
  onRefetch: () => void;
  isFetching?: boolean;
  isNewMigration?: boolean;
  showVoyagerText?: boolean;
  buttonVariant?: "ghost" | "secondary";
}

export const HeadingWithVersion: FC<HeadingWithVersionProps> = ({
  heading,
  voyagerVersion,
  onRefetch,
  showVoyagerText = false,
  buttonVariant = "ghost"
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  return (
    <Box display="flex" justifyContent="space-between" alignItems="center"
      py={`${theme.spacing(4)}px`}>
      <Box display="flex" alignItems="baseline">
        <Typography className={classes.heading}>
          {heading}
        </Typography>
        {showVoyagerText && (
          <>
            <Typography className={classes.hyphen}>
              -
            </Typography>
            <Typography className={classes.voyager}>
              {t("clusterDetail.voyager.planAndAssess.voyagerCapitalize")}
            </Typography>
          </>
        )}
        {!!voyagerVersion && (
          <Typography className={classes.version}>
            {voyagerVersion}
          </Typography>
        )}
      </Box>
      <YBButton variant={buttonVariant} startIcon={<RefreshIcon />} onClick={onRefetch}>
        {t("clusterDetail.performance.actions.refresh")}
      </YBButton>
    </Box>
  );
};
