import React, { FC } from "react";
import { Box } from "@material-ui/core";
import type { Migration } from "./MigrationOverview";
import { MigrationData } from "./phases/MigrationData";
import { MigrationPlanAssess } from "./phases/MigrationPlanAssess";
import { MigrationSchema } from "./phases/MigrationSchema";
import { MigrationVerify } from "./phases/MigrationVerify";

interface MigrationPhaseProps {
  steps: string[];
  migration: Migration;
  phase: number;
}

const phaseComponents = [
  MigrationPlanAssess,
  MigrationSchema,
  MigrationData,
  MigrationVerify,
];

export const MigrationPhase: FC<MigrationPhaseProps> = ({ steps = [""], migration, phase }) => {
  return (
    <Box mt={1}>
      {phaseComponents.map((PhaseComponent, index) => {
        if (index === phase) {
          return (
            <PhaseComponent
              key={index}
              phase={index}
              heading={steps[phase]}
              migration={migration}
            />
          );
        }
        return <React.Fragment key={index} />;
      })}
    </Box>
  );
};
