// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
// End-to-end demonstration of the swapped-data-mount detector on a real tablet server.
//
// fs/fs_root_pin-test proves the verdicts; this file proves the wiring, which is the part a unit
// test cannot reach: that TSTabletManager::Init actually certifies the roots from real superblocks
// (writing the pins), that a tserver booted on physically exchanged data directories refuses to
// start through the ordinary MiniTabletServer::Start status path, and that the refused boot
// created nothing -- no replacement replica, no rewritten pin -- so that swapping the directories
// back is a complete recovery. That last part is the feature's headline claim ("caught one boot
// earlier, while nothing has been lost"), demonstrated rather than asserted.
//
// The swap is done with three directory renames while the server is down, which is exactly what a
// volume swap looks like from inside the filesystem: the contents (including each root's pin file)
// travel, the mount points stay.

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/fs/fs_manager.h"
#include "yb/fs/fs_root_pin.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/faststring.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace integration_tests {

namespace {

const auto kDefaultTimeout = 30000ms;
const char* const kServerType = "tserver";

}  // namespace

class FsRootPinITest : public YBTableTestBase {
 protected:
  int num_drives() override {
    return 3;
  }

  int num_tablets() override {
    return 4;
  }

  size_t num_tablet_servers() override {
    return 3;
  }

  std::string Drive(int drive_index) {
    return mini_cluster()->GetTabletServerDrive(0, drive_index);
  }

  // Sorted tablet-meta entries (i.e. tablet ids with superblocks) under one data root of ts-0.
  Result<std::vector<std::string>> TabletMetaEntries(const std::string& root) {
    const auto dir =
        FsManager::GetRaftGroupMetadataDir(GetServerTypeDataPath(root, kServerType));
    auto entries = VERIFY_RESULT(Env::Default()->GetChildren(dir, ExcludeDots::kTrue));
    std::sort(entries.begin(), entries.end());
    return entries;
  }

  Result<std::string> PinContents(const std::string& root) {
    faststring data;
    RETURN_NOT_OK(ReadFileToString(Env::Default(), FsRootPinPath(root, kServerType), &data));
    return data.ToString();
  }

  // Exchanges two data roots on disk, the way a volume swap presents: contents move, paths stay.
  // Its own inverse, so recovery in the test is a second call.
  Status SwapDrives(const std::string& a, const std::string& b) {
    auto* env = Env::Default();
    const auto tmp = a + ".swapping";
    RETURN_NOT_OK(env->RenameFile(a, tmp));
    RETURN_NOT_OK(env->RenameFile(b, a));
    return env->RenameFile(tmp, b);
  }
};

TEST_F(FsRootPinITest, SwappedDataDirsRefuseStartupWithoutCreatingReplicas) {
  ASSERT_OK(WaitAllReplicasReady(mini_cluster(), kDefaultTimeout));

  auto* ts = mini_cluster()->mini_tablet_server(0);
  const auto drive1 = Drive(0);
  const auto drive2 = Drive(1);

  // --- Certification. The cluster booted on freshly formatted roots, where there is nothing to
  // certify from; the pins are written by TSTabletManager::Init on the first start that finds
  // superblocks. Restart now that tablets exist and expect a pin on every data root: with four
  // tablets assigned least-loaded-first across three drives, no drive is left empty.
  ASSERT_OK(ts->Restart());
  ASSERT_OK(ts->WaitStarted());
  for (int d = 0; d < num_drives(); ++d) {
    const auto pin_path = FsRootPinPath(Drive(d), kServerType);
    ASSERT_TRUE(Env::Default()->FileExists(pin_path))
        << "expected TSTabletManager::Init to have certified " << Drive(d);
  }

  // What is on disk before anything goes wrong. The pin travels with its volume in the swap
  // below, so each captured value is later expected at the other path.
  const auto meta_on_drive1 = ASSERT_RESULT(TabletMetaEntries(drive1));
  const auto meta_on_drive2 = ASSERT_RESULT(TabletMetaEntries(drive2));
  ASSERT_FALSE(meta_on_drive1.empty());
  ASSERT_FALSE(meta_on_drive2.empty());
  const auto pin_of_drive1 = ASSERT_RESULT(PinContents(drive1));
  const auto pin_of_drive2 = ASSERT_RESULT(PinContents(drive2));

  // --- The swap. Refusal must arrive as an ordinary status out of the start path, naming both
  // roots, before tablets are opened.
  ts->Shutdown();
  ASSERT_OK(SwapDrives(drive1, drive2));

  auto status = ts->RestartStoppedServer();
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Refusing to start until the mounts are corrected");
  ASSERT_STR_CONTAINS(status.ToString(), "ts-1-drive-1");
  ASSERT_STR_CONTAINS(status.ToString(), "ts-1-drive-2");

  // --- Nothing was lost and nothing was invented. The refused boot must not have created a
  // replacement replica (the failure mode this detector exists to pre-empt) nor rewritten a pin;
  // the volumes' contents simply sit at exchanged paths.
  ASSERT_EQ(meta_on_drive2, ASSERT_RESULT(TabletMetaEntries(drive1)));
  ASSERT_EQ(meta_on_drive1, ASSERT_RESULT(TabletMetaEntries(drive2)));
  ASSERT_EQ(pin_of_drive2, ASSERT_RESULT(PinContents(drive1)));
  ASSERT_EQ(pin_of_drive1, ASSERT_RESULT(PinContents(drive2)));

  // --- Recovery is what the refusal message promises: put the mounts back and restart. No pin
  // surgery, no metadata surgery.
  ASSERT_OK(SwapDrives(drive1, drive2));
  ASSERT_OK(ts->RestartStoppedServer());
  ASSERT_OK(ts->WaitStarted());
  ASSERT_OK(WaitAllReplicasReady(mini_cluster(), kDefaultTimeout));
  ASSERT_EQ(meta_on_drive1, ASSERT_RESULT(TabletMetaEntries(drive1)));
  ASSERT_EQ(meta_on_drive2, ASSERT_RESULT(TabletMetaEntries(drive2)));
}

}  // namespace integration_tests
}  // namespace yb
