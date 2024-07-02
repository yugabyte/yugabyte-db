package org.yb.minicluster;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;

public class YugabytedTestUtils {

    private static final Logger LOG = LoggerFactory.getLogger(YugabytedTestUtils.class);

    private static final Object flagFilePathLock = new Object();

    private static Path flagFileTmpPath = null;

    private static final String BIN_DIR_PROP = "binDir";

    private static volatile String yugabytedBinariesDir = null;

    /**
     * @return the path of the flags file to pass to yugabyted processes
     *         started by the tests
     */
    public static String getFlagsPath() {
        try {
            synchronized (flagFilePathLock) {
                if (flagFileTmpPath == null || !Files.exists(flagFileTmpPath)) {
                    flagFileTmpPath = Files.createTempFile(
                            Paths.get(TestUtils.getBaseTmpDir()), "ybd-flags", ".conf");
                }
            }
            return flagFileTmpPath.toAbsolutePath().toString();
        } catch (IOException e) {
            throw new RuntimeException("Unable to extract flags file into tmp", e);
        }
    }

    /**
     * @param binName
     *            the binary to look for yugabyted
     * @return the absolute path of that binary
     * @throws FileNotFoundException
     *             if no such binary is found
     */
    public static String findYugabytedBinary(String binName) throws FileNotFoundException {
        String binDir = getYugabytedBinDir();

        File candidate = new File(binDir, binName);
        if (candidate.canExecute()) {
            return candidate.getAbsolutePath();
        }
        throw new FileNotFoundException("Cannot find binary " + binName +
                " in binary directory " + binDir);
    }

    public static String getYugabytedBinDir() {
        if (yugabytedBinariesDir != null)
            return yugabytedBinariesDir;

        // String binDir = System.getProperty(BIN_DIR_PROP);
        // if (binDir != null) {
        // LOG.info("Using binary directory specified by property: {}",
        // binDir);
        // } else {
        // binDir = TestUtils.findYbRootDir() + "/bin";
        // }
        String binDir = TestUtils.findYbRootDir() + "/bin";

        yugabytedBinariesDir = binDir;
        return binDir;
    }

}
