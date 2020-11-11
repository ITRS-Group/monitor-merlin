Given(/^I start naemon$/) do
  # Assume the module exists built in git, if it doesn't, use installed
  merlin_module_path = Dir.pwd + "/.libs/merlin.so"
  if not File.exist?(merlin_module_path) then
    # This path is hardcoded for monitor systems. TODO: make more generic
    merlin_module_path = "/opt/monitor/op5/merlin/merlin.so"
  end
  livestatus_module_path = "/usr/lib64/naemon-livestatus/livestatus.so"
  puts "Using module #{merlin_module_path}"

  output_cmd = "#!/bin/sh\necho -e \"O K\\nL\\nO K|Perf. O K\"\nexit 0"
  step "I have config file output_cmd with permission 777", output_cmd

  active_ok_cmd = "#!/bin/sh\necho -e \"Plugin output\\nLong plugin output|Performance data\"\nexit 0"
  step "I have config file active_ok_cmd with permission 777", active_ok_cmd

  active_critical_cmd = "#!/bin/sh\necho -e \"Plugin output\\nLong plugin output|Performance data\"\nexit 2"
  step "I have config file active_critical_cmd with permission 777", active_critical_cmd

  check_cmd = "#!/bin/sh\necho $@ >> checks.log\n"
  step "I have config file check_cmd with permission 777", check_cmd

  notif_cmd = "#!/bin/sh\necho $@ >> notifications.log\n"
  step "I have config file notif_cmd with permission 777", notif_cmd

  event_handler_cmd = "#!/bin/sh\necho $@ >> event_handlers.log\n"
  step "I have config file event_handler_cmd with permission 777", event_handler_cmd

  steps %Q{
    And I have naemon objects stored in oconf.cfg
    And I have config dir checkresults
    And I have naemon system config file naemon_extra.cfg
    And I have config file naemon.cfg
      """
      cfg_file=oconf.cfg
      broker_module=#{merlin_module_path} merlin.conf
      broker_module=#{livestatus_module_path} log_file=livestatus.log live
      broker_module=/usr/lib64/testthis/testthis.so
      include_file=naemon_extra.cfg
      """
    And I have config file checks.log
      """
      """
    And I start command naemon --allow-root -v naemon.cfg
    And I start daemon naemon --allow-root naemon.cfg
    And I have config file notifications.log
      """
      """
    And I have query handler path naemon.qh
  }

  # Otherwise, it's not deterministic. Make it match default values for CTRL_ACTIVE
  steps %Q{
    And node ipc have info hash my_hash at 5000
    And node ipc have expected hash my_hash at 5000
  }
end

Given(/^I start merlin$/) do
  steps %Q{
    And I have a database running
    And I start daemon merlind -d merlin.conf
    And I wait for 2 seconds
  }
end

Given(/^I have merlin configured for port (\d+)$/) do |port, nodes|
  push_cmd = "#!/bin/sh\necho \"push $@\" >> config_sync.log"
  fetch_cmd = "#!/bin/sh\necho \"fetch $@\" >> config_sync.log"

  configfile = "
    ipc_socket = test_ipc.sock;

    log_level = debug;
    use_syslog = 0;

    oconfsplit_dir = #{@merlinnodeconfig.get_var("oconfsplit_dir")};
    binlog_dir = #{@merlinnodeconfig.get_var("binlog_dir")};
    binlog_max_memory_size = #{@merlinnodeconfig.get_var("binlog_max_memory_size")};
    binlog_max_file_size = #{@merlinnodeconfig.get_var("binlog_max_file_size")};

    module {
      log_file = merlin.log;
      notifies = #{@merlinnodeconfig.get_var("notifies")};
      cluster_update = echo dummy
    }
    daemon {
      pidfile = merlin.pid;
      log_file = merlin.log;
      import_program = /bin/false;
      port = #{port};
      object_config {
        push = ./push_cmd;
        fetch = ./fetch_cmd;
      }
      database {
        name = #{@databaseconfig.name};
        host = #{@databaseconfig.host};
        port = #{@databaseconfig.port};
        user = #{@databaseconfig.user};
        pass = #{@databaseconfig.pass};
      }
    }
    "
  if ENV["MERLIN_ENCRYPTED"] == "TRUE" then
    configfile+= "ipc_privatekey=#{ENV["MERLIN_PRIVKEY"]}"
  end

  nodes.hashes.each do |obj|
    configfile += sprintf "\n%s %s {\n", obj["type"], obj["name"]
    if !obj.include? 'address' then
      configfile += "\taddress = 127.0.0.1\n" # There is no other way in tests
    end
    obj.each do |key, value|
      if key != "type" and key != "name" and value != "ignore" and key != "fetch" then
        configfile += "\t#{key} = #{value}\n"
      end
      if key == "fetch" then
        configfile += "\tobject_config {\n\t\tfetch_name = #{obj["name"]}\n\t\t#{key} = #{value}\n\t}\n"
        # get rid of the general fetch cmd as otherwise it overrides the node
        # specific one
        configfile.sub! 'fetch = ./fetch_cmd;', ''
      end
    end
    if ENV["MERLIN_ENCRYPTED"] == "TRUE" then
      configfile += "\tencrypted = 1\n"
      configfile += "\tpublickey = #{ENV["MERLIN_PUBKEY"]}\n"
    end
    configfile += "}\n"
  end
  step "I have config file config_sync.log", "" # To make sure push steps work
  step "I have config file push_cmd with permission 777", push_cmd
  step "I have config file fetch_cmd with permission 777", fetch_cmd
  step "I have config file merlin.conf", configfile
end

Given(/^node (.*) have ([a-z]*) hash (.*) at ([\d]+)$/) do |node, type, hash, time|
  hexhash = hash.bytes.map { |b| sprintf("%02x",b) }.join
  steps %Q{
    Given I ask query handler merlin testif set #{type} hash #{node} #{hexhash} #{time}
      | filter_var | filter_val | match_var | match_val |
  }
end

# Wrapper, since we specified the path in the naemon start step, we should abstract it out
When(/^I submit the following livestatus query$/) do |query|
	livestatus_socket_path = "live"
	step "I submit the following livestatus query to #{livestatus_socket_path}", query
end
