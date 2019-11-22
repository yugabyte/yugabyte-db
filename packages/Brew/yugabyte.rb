class Yugabyte < Formula
  desc "High-performance distributed SQL database Yugabyte DB"
  homepage "https://yugabyte.com"
  url "https://downloads.yugabyte.com/yugabyte-2.0.5.2-darwin.tar.gz"
  sha256 "8d9b939fe10ba4116c569e27b5b177e9249e448a369dc92b42f5f4a41fa5c8b6"
  depends_on :java => "1.8"
  def install
    rm_f buildpath/"lib/cassandra-driver-internal-only-3.13.0.post0-743d942c.zip"
    rm_f buildpath/"lib/futures-2.1.6-py2.py3-none-any.zip"
    rm_f buildpath/"lib/six-1.7.3-py2.py3-none-any.zip"
    prefix.install Dir["*"]
    bin.install_symlink prefix/"yugabyted"
    bin.install_symlink "ysqlsh"
    bin.install_symlink "cqlsh"
  end
  def post_install
    (var/"yugabyte_data").mkpath
    (var/"log/yugabyte_logs").mkpath
    if !(File.exist?((etc/"yugabyte.conf"))) then
      (etc/"yugabyte.conf").write yugabyte_conf
    end
  end
  def yugabyte_conf; <<~EOS
    {
    "tserver_webserver_port": 9000, 
    "master_rpc_port": 7100, 
    "webserver_port": 7200, 
    "master_webserver_port": 7000, 
    "bind_ip": "127.0.0.1", 
    "ycql_port": 9042, 
    "data_dir": "#{var}/yugabyte_data", 
    "ysql_port": 5433, 
    "log_dir": "#{var}/log/yugabyte_logs", 
    "polling_interval": "5", 
    "tserver_rpc_port": 9100
    }
  EOS
  end
  plist_options :manual => "yugabyted start --config #{HOMEBREW_PREFIX}/etc/yugabyte.conf"
  def plist; <<~EOS
    <?xml version="1.0" encoding="UTF-8"?>
    <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
    <plist version="1.0">
    <dict>
      <key>Label</key>
      <string>#{plist_name}</string>
      <key>ProgramArguments</key>
      <array>
        <string>#{prefix}/yugabyted</string>
        <string>start</string>
        <string>--config</string>
        <string>#{etc}/yugabyte.conf</string>
      </array>
      <key>RunAtLoad</key>
      <true/>
      <key>KeepAlive</key>
      <false/>
      <key>WorkingDirectory</key>
      <string>#{HOMEBREW_PREFIX}</string>
      <key>StandardErrorPath</key>
      <string>#{var}/log/yugabyte_logs/yugabyted.log</string>
      <key>StandardOutPath</key>
      <string>#{var}/log/yugabyte_logs/yugabyted.log</string>
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