package com.yugabyte.yw.commissioner;

import com.google.inject.Inject;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformUniverseNodeConfig;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Scanner;
import java.util.UUID;
import org.yb.perf_advisor.configs.UniverseNodeConfigInterface;
import org.yb.perf_advisor.query.NodeManagerInterface;
import org.yb.perf_advisor.query.commands.CommandInterface;
import org.yb.perf_advisor.query.commands.ExecuteCommand;
import org.yb.perf_advisor.query.commands.FileUploadCommand;

public class PerfAdvisorNodeManager implements NodeManagerInterface {

  private NodeUniverseManager nodeUniverseManager;

  private FileHelperService fileHelperService;

  @Inject
  public PerfAdvisorNodeManager(
      NodeUniverseManager nodeUniverseManager, FileHelperService fileHelperService) {
    this.nodeUniverseManager = nodeUniverseManager;
    this.fileHelperService = fileHelperService;
  }

  @Override
  public String executeCommands(
      UniverseNodeConfigInterface nodeConfig, List<CommandInterface> commands) throws IOException {
    PlatformUniverseNodeConfig universeConfig = (PlatformUniverseNodeConfig) nodeConfig;
    Path tempPath = null;
    try {
      Universe universe = universeConfig.getUniverse();
      ShellResponse response = null;
      for (CommandInterface command : commands) {
        if (command instanceof ExecuteCommand) {
          ExecuteCommand execCommandObj = (ExecuteCommand) command;
          List<String> commandArgList = execCommandObj.getFullCommand();
          response =
              nodeUniverseManager
                  .runCommand(
                      universeConfig.getNodeDetails(),
                      universe,
                      commandArgList,
                      universeConfig.getShellProcessContext())
                  .processErrors();
        } else if (command instanceof FileUploadCommand) {
          FileUploadCommand fileUpload = (FileUploadCommand) command;
          try (InputStream s =
                  Class.forName("org.yb.perf_advisor.Utils")
                      .getClassLoader()
                      .getResourceAsStream(fileUpload.getSrcFilePath());
              Scanner scanner = new Scanner(s)) {
            String pythonText = scanner.useDelimiter("\\A").next();
            String prefix = String.format("pa_script_%s", UUID.randomUUID());
            tempPath = fileHelperService.createTempFile(prefix, ".py");
            Files.write(tempPath, pythonText.getBytes(StandardCharsets.UTF_8));
            response =
                nodeUniverseManager
                    .uploadFileToNode(
                        universeConfig.getNodeDetails(),
                        universe,
                        tempPath.toString(),
                        fileUpload.getDestFilePath(),
                        fileUpload.getFilePermissions(),
                        universeConfig.getShellProcessContext())
                    .processErrors();
          }
        }
      }
      return response.extractRunCommandOutput();
    } catch (ClassNotFoundException e) {
      throw new IOException("ClassLoader failed in to retrieve info from module.");
    } finally {
      if (tempPath != null) {
        Files.deleteIfExists(tempPath);
      }
    }
  }
}
