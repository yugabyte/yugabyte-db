import React, { FC } from "react";
import { Box, makeStyles, Paper, Typography, useTheme } from "@material-ui/core";
import HandWaveIcon from "@app/assets/handwave.png";
import { useTranslation } from "react-i18next";

const useStyles = makeStyles((theme) => ({
  paper: {
    boxShadow: "0px 8px 16px 0px rgba(0, 0, 0, 0.08)",
    border: "1px solid rgba(233, 238, 242, 1)",
    marginBottom: theme.spacing(2.5),
  },
  handwave: {
    flexShrink: 0,
    height: "fit-content",
  },
  heading: {
    color: "#5D5FEF",
  },
  dot: {
    width: 8,
    height: 8,
    borderRadius: "50%",
    backgroundColor: theme.palette.grey[300],
    flexShrink: 0,
  },
}));

interface PrereqsProps {
  items: React.ReactNode[];
}

export const Prereqs: FC<PrereqsProps> = ({ items }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  return (
    <Paper className={classes.paper}>
      <Box p={3} display="flex" gridGap={theme.spacing(3)}>
        <img src={HandWaveIcon} className={classes.handwave} />
        <Box>
          <Typography variant="h5" className={classes.heading}>
            {t("clusterDetail.voyager.migrateSchema.beforeYouProceed")}
          </Typography>
          <Box mx={1}>
            {items.map((item, index) => (
              <Box
                key={index}
                display="flex"
                alignItems="center"
                gridGap={theme.spacing(2)}
                mt={1.5}
              >
                <Box className={classes.dot} />
                <Typography variant="body2">{item}</Typography>
              </Box>
            ))}
          </Box>
        </Box>
      </Box>
    </Paper>
  );
};
