Given(/^I start naemon$/) do
  # Assume the module exists built in git, if it doesn't, use installed
  merlin_module_path = Dir.pwd + "/.libs/merlin.so"
  if not File.exist?(merlin_module_path) then
    # This path is hardcoded for monitor systems. TODO: make more generic
    merlin_module_path = "/opt/monitor/op5/merlin/merlin.so"
  end
  puts "Using module #{merlin_module_path}"
  steps %Q{
    And I have naemon objects stored in oconf.cfg
    And I have config dir checkresults
    And I have config file naemon.cfg
      """
      cfg_file=oconf.cfg
      query_socket=naemon.qh
      check_result_path=checkresults
      broker_module=#{merlin_module_path} merlin.conf
      event_broker_options=-1
      command_file=naemon.cmd
      """
    And I start daemon naemon naemon.cfg
    And I have query handler path naemon.qh
  }
end

Given(/^I start merlin$/) do
  step "I start daemon merlind -d merlin.conf"
end

Given(/^I have merlin configured for port (\d+)$/) do |port, nodes|
  configfile = "
    ipc_socket = test_ipc.sock;

    log_level = info;
    use_syslog = 1;

    module {
      log_file = /dev/stdout
    }
    daemon {
      pidfile = merlin.pid;
      log_file = /dev/stdout
      import_program = /bin/false
      port = #{port};
      object_config {
        dump = /bin/false
      }
    }
    "
  nodes.hashes.each do |obj|
    configfile += sprintf "\n%s %s {\n", obj["type"], obj["name"]
    configfile += "\taddress = 127.0.0.1\n" # There is no other way in tests
    obj.each do |key, value|
      if key != "type" and key != "name" then
        configfile += "\t#{key} = #{value}\n"
      end
    end
    configfile += "}\n"
  end
  step "I have config file merlin.conf", configfile
end