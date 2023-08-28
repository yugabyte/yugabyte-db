import React, { FC } from "react";
import { Typography, makeStyles, Box, Card, CardActionArea } from "@material-ui/core";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import clsx from "clsx";
import { YBTooltip } from "@app/components";

const useStyles = makeStyles((theme) => ({
  wrapper: {
    margin: theme.spacing(1, 0, 1, 0),
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(4),
    flexWrap: "wrap",
  },
  heading: {
    margin: theme.spacing(0, 0, 0, 0),
  },
  icon: {
    height: theme.spacing(4),
    width: theme.spacing(4),
    borderRadius: "50%",
    backgroundColor: theme.palette.grey[300],
    color: theme.palette.grey[900],
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    fontSize: "15px",
    lineHeight: "18px",
    fontWeight: 700,
  },
  highlight: {
    color: theme.palette.primary["700"],
    fontWeight: 600,
  },
  card: {
    boxShadow: "none",
  },
  cardActionArea: {
    padding: theme.spacing(2),
    display: "flex",
    flexDirection: "column",
    gridGap: theme.spacing(2),
    alignItems: "start",
    position: "relative",
  },
  selected: {
    background: theme.palette.grey[100],
  },
  notStarted: {
    color: theme.palette.grey[700],
  },
  disabledOverlay: {
    position: "absolute",
    top: 0,
    right: 0,
    width: "100%",
    height: "100%",
    background: "rgba(255, 255, 255, 0.5)",
    pointerEvents: "none",
  },
  disabledCard: {
    borderColor: theme.palette.grey[200],
    "&:hover": {
      cursor: "not-allowed",
    },
  },
}));

interface MigrationTilesProps {
  steps: string[];
  currentStep?: number;
  runningStep?: number;
  onStepChange?: (step: number) => void;
}

export const MigrationTiles: FC<MigrationTilesProps> = ({
  steps,
  onStepChange,
  currentStep,
  runningStep,
}) => {
  const { t } = useTranslation();
  const classes = useStyles();

  return (
    <Box className={classes.wrapper}>
      {/* <Typography variant="h4" className={classes.heading}>
        {t('clusterDetail.voyager.phases')}
      </Typography> */}
      {steps.map((step, index) => {
        const notStarted = runningStep != null && index > runningStep;
        return (
          <Card className={clsx(classes.card, notStarted && classes.disabledCard)}>
            <CardActionArea
              className={clsx(classes.cardActionArea, currentStep === index && classes.selected)}
              disabled={notStarted}
              onClick={() => onStepChange && onStepChange(index)}
            >
              {notStarted && <Box className={classes.disabledOverlay} />}
              <Box display="flex" alignItems="center" gridGap={10}>
                {/* <span className={classes.icon}>{(index + 1).toString()}</span> */}
                <Typography variant="h5" color={notStarted ? "textSecondary" : undefined}>
                  {step}
                </Typography>
                <Box ml={-1}>
                  <YBTooltip title={step} />
                </Box>
              </Box>
              {runningStep != null && (index < runningStep || runningStep === steps.length - 1) && (
                <YBBadge
                  variant={BadgeVariant.Success}
                  text={t("clusterDetail.voyager.complete")}
                />
              )}
              {runningStep != null && runningStep === index && runningStep !== steps.length - 1 && (
                <YBBadge variant={BadgeVariant.InProgress} />
              )}

              {notStarted && (
                <Box mt={0.8} mb={0.4} className={classes.notStarted}>
                  {t("clusterDetail.voyager.notStarted")}
                </Box>
              )}
            </CardActionArea>
          </Card>
        );
      })}
    </Box>
  );
};
