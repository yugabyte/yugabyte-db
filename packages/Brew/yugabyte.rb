class Yugabyte < Formula
  desc "High-performance distributed SQL database Yugabyte DB"
  homepage "https://yugabyte.com"
  url "https://downloads.yugabyte.com/yugabyte-2.0.3.0-darwin.tar.gz"
  sha256 "5d7d35b3bbdee7b89fe7910a363734b559cd5ce7e5899cd155024fd965fd8145"

  def install
    rm_f buildpath/"lib/cassandra-driver-internal-only-3.13.0.post0-743d942c.zip"
    rm_f buildpath/"lib/futures-2.1.6-py2.py3-none-any.zip"
    rm_f buildpath/"lib/six-1.7.3-py2.py3-none-any.zip"
    prefix.install Dir["*"]
    bin.install_symlink prefix/"yugabyte-db"
  end

  plist_options :manual => "yugabyte-db start"

  def plist; <<~EOS
    <?xml version="1.0" encoding="UTF-8"?>
    <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
    <plist version="1.0">
    <dict>
      <key>Label</key>
      <string>#{plist_name}</string>
      <key>ProgramArguments</key>
      <array>
        <string>#{prefix}/yugabyte-db</string>
        <string>start</string>
      </array>
      <key>RunAtLoad</key>
      <true/>
      <key>KeepAlive</key>
      <false/>
      <key>WorkingDirectory</key>
      <string>#{prefix}</string>
      <key>HardResourceLimits</key>
      <dict>
        <key>NumberOfFiles</key>
        <integer>262144</integer>
      </dict>
      <key>SoftResourceLimits</key>
      <dict>
        <key>NumberOfFiles</key>
        <integer>262144</integer>
      </dict>
    </dict>
    </plist>
  EOS
  end

  test do
    system "#{prefix}/yugabyte-db", "help"
  end
end
