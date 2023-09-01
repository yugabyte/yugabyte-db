import React, { FC } from "react";
import { Typography, makeStyles, Box, Card, CardActionArea } from "@material-ui/core";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import clsx from "clsx";
import { STATUS_TYPES, YBStatus, YBTooltip } from "@app/components";
import { MigrationPhase, MigrationStep, migrationPhases, migrationSteps } from "./migration";

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
  phase?: number;
  onStepChange?: (step: number) => void;
}

export const MigrationTiles: FC<MigrationTilesProps> = ({
  steps,
  onStepChange,
  currentStep,
  phase,
}) => {
  const { t } = useTranslation();
  const classes = useStyles();

  const getTooltip = (step: string) => {
    if (step === migrationSteps[MigrationStep["Plan and Assess"]]) {
      return migrationPhases[MigrationPhase["Analyze Schema"]];
    } else if (step === migrationSteps[MigrationStep["Migrate Schema"]]) {
      return [
        migrationPhases[MigrationPhase["Analyze Schema"]],
        migrationPhases[MigrationPhase["Export Schema"]],
        migrationPhases[MigrationPhase["Import Schema"]],
      ].join(", ");
    } else if (step === migrationSteps[MigrationStep["Migrate Data"]]) {
      return [
        migrationPhases[MigrationPhase["Export Data"]],
        migrationPhases[MigrationPhase["Import Data"]],
      ].join(", ");
    } else if (step === migrationSteps[MigrationStep["Verify"]]) {
      return migrationPhases[MigrationPhase["Verify"]];
    } else {
      return step;
    }
  };

  const isFetching = false;

  return (
    <Box className={classes.wrapper}>
      {steps.map((step, index) => {
        let disabled = false;
        let notStarted = false;
        let completed = false;
        let running = false;

        // Phase 0, 1    === Plan and Assess & Migrate Schema                         steps active
        // Phase 2, 3, 4 === Plan and Assess & Migrate Schema & Migrate Data          steps active
        // Phase 5       === Plan and Assess & Migrate Schema & Migrate Data & Verify steps active

        if (phase != null) {
          // Export schema phase
          if (phase === MigrationPhase["Export Schema"]) {
            if (index >= MigrationStep["Migrate Data"]) {
              // Everything except first two steps will be pending and disabled
              notStarted = true;
              disabled = true;
            } else if (index === MigrationStep["Plan and Assess"]) {
              // Plan and assess will be pending
              notStarted = true;
            } else {
              // Migrate schema will be running
              running = true;
            }
          }

          // Analyze schema phase
          else if (phase === MigrationPhase["Analyze Schema"]) {
            if (index >= MigrationStep["Migrate Data"]) {
              // Everything except first two steps will be pending and disabled
              disabled = true;
              notStarted = true;
            } else if (index === MigrationStep["Migrate Schema"]) {
              // Migrate schema will be running
              running = true;
            } else {
              // Plan and assess will be completed
              completed = true;
            }
          }

          // Export data and Import schema phase
          else if (phase <= MigrationPhase["Import Schema"]) {
            if (index === MigrationStep["Plan and Assess"]) {
              // Plan and assess will be completed
              completed = true;
            } else if (index <= MigrationStep["Migrate Data"]) {
              // Migrate schema and Migrate data will be running
              running = true;
            } else {
              // Verify will be pending and disabled
              disabled = true;
              notStarted = true;
            }
          }

          // Import schema phase
          else if (phase === MigrationPhase["Import Data"]) {
            if (index <= MigrationStep["Migrate Schema"]) {
              // Plan and assess and Migrate schema will be completed
              completed = true;
            } else if (index === MigrationStep["Migrate Data"]) {
              // Migrate data will be running
              running = true;
            } else {
              // Verify will be pending and disabled
              disabled = true;
              notStarted = true;
            }
          }

          // Verify phase
          else {
            // Everything will be completed
            completed = true;
          }
        }

        return (
          <Card key={step} className={clsx(classes.card, disabled && classes.disabledCard)}>
            <CardActionArea
              className={clsx(classes.cardActionArea, currentStep === index && classes.selected)}
              disabled={disabled}
              onClick={() => onStepChange && onStepChange(index)}
            >
              {disabled && <Box className={classes.disabledOverlay} />}
              <Box display="flex" alignItems="center" gridGap={10}>
                {/* <span className={classes.icon}>{(index + 1).toString()}</span> */}
                <Typography variant="h5">{step}</Typography>
                <Box ml={-1}>
                  <YBTooltip title={getTooltip(step)} />
                </Box>
              </Box>
              {isFetching ? (
                <YBStatus
                  type={STATUS_TYPES.IN_PROGRESS}
                  label={t("clusterDetail.voyager.refreshing")}
                />
              ) : (
                <>
                  {completed && (
                    <YBBadge
                      variant={BadgeVariant.Success}
                      text={t("clusterDetail.voyager.complete")}
                    />
                  )}
                  {running && <YBBadge variant={BadgeVariant.InProgress} />}
                  {notStarted && (
                    <Box mt={0.8} mb={0.4} className={classes.notStarted}>
                      {t("clusterDetail.voyager.notStarted")}
                    </Box>
                  )}
                </>
              )}
            </CardActionArea>
          </Card>
        );
      })}
    </Box>
  );
};
