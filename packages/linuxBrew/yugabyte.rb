class Yugabyte < Formula
  desc "Formula for Yugabyte DB"
  homepage "https://www.yugabyte.com/"
  url "https://downloads.yugabyte.com/yugabyte-2.0.3.0-linux.tar.gz"
  sha256 "aebb55c60a8a67abac0b5cd06b2cb4dcccfbafdfa878f0d7fd32ca3d7f92285b"

  bottle :unneeded
  depends_on "glibc"
  depends_on :java => "1.8+"
  depends_on "patchelf"
  def install
    libexec.install Dir["*"]
    bin.install_symlink libexec/"yugabyte-db"
    bin.install_symlink libexec/"bin/ysqlsh"
    bin.install_symlink libexec/"bin/ycqlsh"
  end

  test do
    system "#{prefix}/yugabyte-db", "help"
  end
end
