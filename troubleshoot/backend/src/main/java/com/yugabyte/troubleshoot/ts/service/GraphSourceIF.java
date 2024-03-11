package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.models.*;
import java.util.*;

public interface GraphSourceIF {
  boolean supportsGraph(String name);

  long minGraphStepSeconds(UniverseMetadata universeMetadata);

  GraphResponse getGraph(
      UniverseMetadata universeMetadata, UniverseDetails universeDetails, GraphQuery query);
}
