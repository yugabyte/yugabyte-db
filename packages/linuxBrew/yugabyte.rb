class Yugabyte < Formula
  desc "Formula for Yugabyte DB"
  homepage "https://www.yugabyte.com/"
  url "https://downloads.yugabyte.com/yugabyte-2.0.5.2-linux.tar.gz"
  sha256 "2506c70b05ac742a75922fc51407732150192e82d1e55adc47df85ec75a2ed09"

  bottle :unneeded
  depends_on "glibc"
  depends_on :java => "1.8+"
  depends_on "patchelf"
  def install
    libexec.install Dir["*"]
    bin.install_symlink libexec/"yugabyted"
    bin.install_symlink libexec/"bin/ysqlsh"
    bin.install_symlink libexec/"bin/cqlsh"
  end

  test do
    system "#{prefix}/yugabyted", "help"
  end
end
