class NaemonSystemConfig
  def initialize
    @current_config = {
      "query_socket" => "naemon.qh",
      "check_result_path" => "checkresults",
      "log_file" => "livestatus.log live",
      "event_broker_options" => "-1",
      "command_file" => "naemon.cmd",
      "object_cache_file" => "objects.cache",
      "status_file" => "/dev/null",
      "log_file" => "naemon.log",
      "retain_state_information" => "1",
      "state_retention_file" => "status.sav",
      "execute_host_checks" => "0",
      "execute_service_checks" => "0"
    }
  end

  def set_var(name, value)
    @current_config[name] = value
  end

  def configfile
    res = ""
    @current_config.each do |name, value|
      res += "#{name}=#{value}\n"
    end
    res
  end
end

Before do
  # Create a global variable, that we can easily access it in the step definitions.
  # However, the variable is intended to be accessed exclusively in the
  # features/step_definitions/naemon_system_config.rb step definition for a
  # neat logical isolation.
  @naemonsysconfig = NaemonSystemConfig.new
end
