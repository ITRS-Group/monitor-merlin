# Make sure to use binaries in current working directory first, so we use newly
# compiled  in first case. Use the absolute path, so we can change working
# directory later.
#
# If started through CI, both cukemerlin and merlind is available installed, so
# it will use them in that case

testdir_path = Dir.pwd
ENV['PATH'] = "#{testdir_path}:#{ENV['PATH']}"

if ENV['CUKEMERLIN_AUTOSTART'] != "no" then
  # Spawn cukemerlin (cucumber wire daemon)
  cukemerlin = Process.spawn("cukemerlin --bind-port=31221")

  # Give cukemerlin time to start listen to the socket
  sleep(2)

  at_exit do
    Process.kill("TERM", cukemerlin)
    Process.wait
  end
end