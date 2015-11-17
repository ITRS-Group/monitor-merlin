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
  }
end