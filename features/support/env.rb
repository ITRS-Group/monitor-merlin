# Spawn cukemerlin (cucumber wire daemon)
cukemerlin = Process.spawn("cukemerlin --bind-port=31221")

# Give cukemerlin time to start listen to the socket
sleep(2)

at_exit do
	Process.kill("TERM", cukemerlin)
	Process.wait
end
