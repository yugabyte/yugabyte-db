// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.helm;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.FileHelperService;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermission;
import java.util.Comparator;
import java.util.Set;
import java.util.stream.Stream;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.IOUtils;

/**
 * Builds a Helm <a href="https://helm.sh/docs/topics/advanced/#post-rendering">post-renderer</a>
 * that injects a root init container into the yb-tserver StatefulSet to hand {@code
 * /mnt/disk0/pg_data} back to UID 0.
 */
@Slf4j
@Singleton
public class PgDataOwnershipPostRenderer {

  public static final String INIT_CONTAINER_NAME = "pgdata-root-ownership-reconcile";

  static final String PATCH_TEMPLATE = "helm/pgdata-ownership-patch.yaml.template";
  static final String KUSTOMIZATION_TEMPLATE = "helm/pgdata-ownership-kustomization.yaml.template";
  static final String POST_RENDER_SCRIPT = "helm/post-render.sh";
  private static final String PATCH_FILE = "patch.yaml";
  private static final String KUSTOMIZATION_FILE = "kustomization.yaml";
  private static final String SCRIPT_FILE = "post-render.sh";

  private static final Set<PosixFilePermission> SCRIPT_PERMISSIONS =
      Set.of(
          PosixFilePermission.OWNER_READ,
          PosixFilePermission.OWNER_WRITE,
          PosixFilePermission.OWNER_EXECUTE);

  private final FileHelperService fileHelperService;
  private final play.Environment environment;

  @Inject
  public PgDataOwnershipPostRenderer(
      FileHelperService fileHelperService, play.Environment environment) {
    this.fileHelperService = fileHelperService;
    this.environment = environment;
  }

  public static class PostRenderer {
    @Getter private final String scriptPath;
    private final Path directory;

    PostRenderer(Path directory, Path scriptPath) {
      this.directory = directory;
      this.scriptPath = scriptPath.toAbsolutePath().toString();
    }

    public void cleanup() {
      try (Stream<Path> paths = Files.walk(directory)) {
        paths.sorted(Comparator.reverseOrder()).forEach(PgDataOwnershipPostRenderer::deleteQuietly);
      } catch (IOException e) {
        log.warn("Could not clean up post-renderer directory {}", directory, e);
      }
    }
  }

  /** Materializes the post-renderer on the YBA host. */
  public PostRenderer create(
      boolean newNamingStyle, String image, String imagePullPolicy, String dataVolumeName) {
    Path directory = fileHelperService.createTempDirectory("yb-pgdata-post-render");
    try {
      Files.writeString(
          directory.resolve(PATCH_FILE), renderPatch(image, imagePullPolicy, dataVolumeName));
      Files.writeString(directory.resolve(KUSTOMIZATION_FILE), renderKustomization(newNamingStyle));

      Path scriptPath = directory.resolve(SCRIPT_FILE);
      Files.writeString(scriptPath, readResource(POST_RENDER_SCRIPT));
      // Helm execs this directly, so it has to carry the execute bit.
      Files.setPosixFilePermissions(scriptPath, SCRIPT_PERMISSIONS);

      log.info(
          "Created pg_data ownership post-renderer at {} (image={}, volume={})",
          scriptPath,
          image,
          dataVolumeName);
      return new PostRenderer(directory, scriptPath);
    } catch (IOException e) {
      throw new RuntimeException("Could not create the pg_data ownership post-renderer", e);
    }
  }

  String renderPatch(String image, String imagePullPolicy, String dataVolumeName) {
    return readResource(PATCH_TEMPLATE)
        .replace("{{DB_IMAGE}}", image)
        .replace("{{IMAGE_PULL_POLICY}}", imagePullPolicy)
        .replace("{{DATA_VOLUME_NAME}}", dataVolumeName);
  }

  String renderKustomization(boolean newNamingStyle) {
    String appLabel = newNamingStyle ? "app.kubernetes.io/name" : "app";
    return readResource(KUSTOMIZATION_TEMPLATE).replace("{{TSERVER_APP_LABEL}}", appLabel);
  }

  private String readResource(String resource) {
    try (InputStream stream = environment.resourceAsStream(resource)) {
      if (stream == null) {
        throw new IOException("Resource not found");
      }
      return IOUtils.toString(stream, StandardCharsets.UTF_8);
    } catch (IOException e) {
      throw new RuntimeException("Could not read " + resource, e);
    }
  }

  private static void deleteQuietly(Path path) {
    try {
      Files.deleteIfExists(path);
    } catch (IOException e) {
      log.warn("Could not delete {}", path, e);
    }
  }
}
