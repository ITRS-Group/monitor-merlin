class MerlinNodeConfig

  def initialize
    @current_config = {
      "notifies" => "yes",
      "binlog_dir" => ".",
      "oconfsplit_dir" => ".",
      "binlog_max_file_size" => "5000",
      "binlog_max_memory_size" => "500",
      "binlog_persist" => "1",
      "ipc_blocked_hostgroups" => ""
    }
  end

  def set_var(name, value)
    @current_config[name] = value
  end

  def get_var(name)
    res = @current_config[name]
    res
  end
end

Before do
  # Create a global variable, that we can easily access it in the step definitions.
  # However, the variable is intended to be accessed exclusively in the
  # features/step_definitions/naemon_system_config.rb step definition for a
  # neat logical isolation.
  @merlinnodeconfig = MerlinNodeConfig.new
end
