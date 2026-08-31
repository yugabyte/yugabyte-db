// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.helm;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import org.apache.commons.io.IOUtils;
import org.junit.Test;
import org.yaml.snakeyaml.Yaml;
import play.Environment;
import play.Mode;

public class PgDataOwnershipPostRendererTest {

  private final Environment environment =
      new Environment(new File("."), getClass().getClassLoader(), Mode.TEST);

  // The renderers only read bundled resources and substitute placeholders, so no FileHelperService
  // is needed.
  private final PgDataOwnershipPostRenderer renderer =
      new PgDataOwnershipPostRenderer(null, environment);

  @SuppressWarnings("unchecked")
  private Map<String, Object> initContainer(String patchYaml) {
    Map<String, Object> patch = new Yaml().load(patchYaml);
    Map<String, Object> spec = (Map<String, Object>) patch.get("spec");
    Map<String, Object> template = (Map<String, Object>) spec.get("template");
    Map<String, Object> podSpec = (Map<String, Object>) template.get("spec");
    List<Map<String, Object>> initContainers =
        (List<Map<String, Object>>) podSpec.get("initContainers");
    assertEquals("expected exactly one init container in the patch", 1, initContainers.size());
    return initContainers.get(0);
  }

  @SuppressWarnings("unchecked")
  private Map<String, Object> patchTarget(String kustomizationYaml) {
    Map<String, Object> kustomization = new Yaml().load(kustomizationYaml);
    List<Map<String, Object>> patches = (List<Map<String, Object>>) kustomization.get("patches");
    assertEquals(1, patches.size());
    return (Map<String, Object>) patches.get(0).get("target");
  }

  @Test
  public void testKustomizationTargetsTserverStsNewNamingStyle() {
    Map<String, Object> target = patchTarget(renderer.renderKustomization(true));
    assertEquals("apps", target.get("group"));
    assertEquals("v1", target.get("version"));
    assertEquals("StatefulSet", target.get("kind"));
    assertEquals("app.kubernetes.io/name=yb-tserver", target.get("labelSelector"));
  }

  @Test
  public void testKustomizationTargetsTserverStsOldNamingStyle() {
    Map<String, Object> target = patchTarget(renderer.renderKustomization(false));
    assertEquals("app=yb-tserver", target.get("labelSelector"));
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testPatchAddsRootInitContainerMountingDisk0() {
    String patchYaml =
        renderer.renderPatch("yugabytedb/yugabyte:2025.2.5.1-b1", "IfNotPresent", "datadir0");
    Map<String, Object> patch = new Yaml().load(patchYaml);
    assertEquals("apps/v1", patch.get("apiVersion"));
    assertEquals("StatefulSet", patch.get("kind"));

    Map<String, Object> container = initContainer(patchYaml);
    assertEquals(PgDataOwnershipPostRenderer.INIT_CONTAINER_NAME, container.get("name"));
    assertEquals("yugabytedb/yugabyte:2025.2.5.1-b1", container.get("image"));
    assertEquals("IfNotPresent", container.get("imagePullPolicy"));

    // Has to be root: changing a file's owner UID needs CAP_CHOWN.
    Map<String, Object> securityContext = (Map<String, Object>) container.get("securityContext");
    assertEquals(Integer.valueOf(0), securityContext.get("runAsUser"));
    assertEquals(Integer.valueOf(0), securityContext.get("runAsGroup"));

    List<Map<String, Object>> mounts = (List<Map<String, Object>>) container.get("volumeMounts");
    assertEquals(1, mounts.size());
    assertEquals("datadir0", mounts.get(0).get("name"));
    assertEquals("/mnt/disk0", mounts.get(0).get("mountPath"));
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testPatchUsesTheReleasePrefixedVolumeNameWhenGiven() {
    List<Map<String, Object>> mounts =
        (List<Map<String, Object>>)
            initContainer(renderer.renderPatch("img:tag", "Always", "yb-test-yugabyte-datadir0"))
                .get("volumeMounts");
    assertEquals("yb-test-yugabyte-datadir0", mounts.get(0).get("name"));
  }

  @Test
  public void testEveryPlaceholderIsSubstituted() {
    String patchYaml = renderer.renderPatch("img:tag", "IfNotPresent", "datadir0");
    // A typo in a placeholder name would otherwise ship a literal {{...}} into the manifest.
    assertFalse("unsubstituted placeholder in patch: " + patchYaml, patchYaml.contains("{{"));
    String kustomizationYaml = renderer.renderKustomization(true);
    assertFalse(kustomizationYaml.contains("{{"));
  }

  @SuppressWarnings("unchecked")
  private String reconcileScript() {
    Map<String, Object> container =
        initContainer(renderer.renderPatch("img:tag", "IfNotPresent", "datadir0"));
    List<String> command = (List<String>) container.get("command");
    assertEquals("/bin/bash", command.get(0));
    assertEquals("-c", command.get(1));
    return command.get(2);
  }

  @Test
  public void testReconcileScriptCoversEveryPgDataDir() {
    String script = reconcileScript();
    // pg_data is a symlink to a version specific directory, and a rollback that is also a YSQL
    // major downgrade re-links it, so every pg_data* has to be covered. The glob does that in one
    // shot - and it is load bearing: "chown -R" on the symlink alone would not traverse into its
    // target, because GNU chown defaults to -P.
    assertTrue(script.contains("pgdata=(/mnt/disk0/pg_data*)"));
    assertTrue(script.contains("chown -R 0:0 \"${targets[@]}\""));
    // nullglob is what makes the empty case detectable rather than a literal unmatched pattern.
    assertTrue(script.contains("shopt -s nullglob"));
    assertTrue(script.contains("if [ ${#targets[@]} -eq 0 ]; then"));
    // Fail the pod rather than letting PostgreSQL FATAL-loop.
    assertTrue(script.contains("set -euo pipefail"));
    // The shell expansions must survive templating verbatim.
    assertTrue(script.contains("${targets[*]}"));
  }

  /**
   * PostgreSQL refuses to start unless its TLS private key is owned by the DB user or by root -
   * check_ssl_key_file_permissions() in be-secure-common.c. The forward migration leaves
   * /mnt/disk0/certs owned by 10001, and the older chart's certmanager-init copies onto the
   * existing files, which preserves their ownership, so the certs have to be reclaimed too.
   */
  @Test
  public void testReconcileScriptAlsoReclaimsTheTlsCerts() {
    String script = reconcileScript();
    // Guarded, because a universe without TLS has no certs directory at all.
    assertTrue(script.contains("if [ -d /mnt/disk0/certs ]; then"));
    assertTrue(script.contains("targets+=(/mnt/disk0/certs)"));
  }

  /**
   * The data directory's mode is only a warning in YugabyteDB (downgraded in 2fce99e437, [#6131]),
   * but a root owned private key with group or world access is a hard failure. fsGroup leaves both
   * group-writable, so both need fixing - and the certs must not get the data directory's mode.
   */
  @Test
  public void testReconcileScriptFixesModesForBothDataDirAndKeys() {
    String script = reconcileScript();
    // 0750 clears PG_MODE_MASK_GROUP, which is (S_IWGRP | S_IRWXO).
    assertTrue(script.contains("chmod 0750 \"${pgdata[@]}\""));
    // Only the data directories, never the certs directory, which certmanager-init chmods itself.
    assertFalse(script.contains("chmod 0750 \"${targets[@]}\""));
    assertTrue(script.contains("keys=(/mnt/disk0/certs/*.key)"));
    assertTrue(script.contains("chmod 600 \"${keys[@]}\""));
  }

  /**
   * The three bundled files only work together if they agree on file names: post-render.sh writes
   * Helm's stdin to manifest.yaml, and the kustomization consumes exactly that plus patch.yaml.
   */
  @Test
  public void testBundledFilesAgreeOnFileNames() throws Exception {
    String script;
    try (InputStream stream =
        environment.resourceAsStream(PgDataOwnershipPostRenderer.POST_RENDER_SCRIPT)) {
      script = IOUtils.toString(stream, StandardCharsets.UTF_8);
    }
    assertTrue("post-render.sh must capture stdin", script.contains("cat > manifest.yaml"));
    assertTrue("post-render.sh must invoke kustomize", script.contains("kubectl kustomize"));

    String kustomizationYaml = renderer.renderKustomization(true);
    assertTrue(kustomizationYaml.contains("manifest.yaml"));
    assertTrue(kustomizationYaml.contains("patch.yaml"));
  }
}
