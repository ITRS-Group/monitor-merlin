class NaemonObjectConfig
  def initialize
    @current_config = {
      "contact" => [{
        "name" => "default-contact",
        "contact_name" => "default-contact",
        "host_notifications_enabled" => "1",
        "service_notifications_enabled" => "1",
        "host_notification_period" => "24x7",
        "service_notification_period" => "24x7",
        "host_notification_options" => "a",
        "service_notification_options" => "a",
        "host_notification_commands" => "log_notif_host",
        "service_notification_commands" => "log_notif_service",
      }],
      "timeperiod" => [{
        "timeperiod_name" => "24x7",
        "alias" => "24x7",
        "sunday" => "00:00-24:00",
        "monday" => "00:00-24:00",
        "tuesday" => "00:00-24:00",
        "wednesday" => "00:00-24:00",
        "thursday" => "00:00-24:00",
        "friday" => "00:00-24:00",
        "saturday" => "00:00-24:00"
      }],
      "command" => [{
        "command_name" => "log_check_host",
        "command_line" => "./check_cmd check host $HOSTNAME$"
      },{
        "command_name" => "log_check_service",
        "command_line" => "./check_cmd check service $HOSTNAME$ $SERVICEDESC$"
      },{
        "command_name" => "log_notif_host",
        "command_line" => "./check_cmd notif host $HOSTNAME$ $NOTIFICATIONCOMMENT$"
      },{
        "command_name" => "log_notif_service",
        "command_line" => "./check_cmd notif service $HOSTNAME$ $SERVICEDESC$ $NOTIFICATIONCOMMENT$"
      },{
        "command_name" => "check_long_output",
        "command_line" => "./output_cmd"
      }],
      "host" => [{
        "check_command" => "log_check_host",
        "max_check_attempts" => 3,
        "check_interval" => 5,
        "retry_interval" => 0,
        "obsess" => 0,
        "check_freshness" => 0,
        "active_checks_enabled" => 0,
        "passive_checks_enabled" => 1,
        "event_handler_enabled" => 1,
        "flap_detection_enabled" => 1,
        "process_perf_data" => 1,
        "retain_status_information" => 1,
        "retain_nonstatus_information" => 1,
        "notification_interval" => 0,
        "notification_options" => "a",
        "notifications_enabled" => 1,
        "register" => 0,
        "name" => "default-host",
      }],
      "service" => [{
        "is_volatile" => 0,
        "max_check_attempts" => 3,
        "check_interval" => 5,
        "check_command" => "log_check_service",
        "retry_interval" => 1,
        "active_checks_enabled" => 0,
        "passive_checks_enabled" => 1,
        "check_period" => "24x7",
        "parallelize_check" => 0,
        "obsess" => 0,
        "check_freshness" => 0,
        "event_handler_enabled" => 1,
        "flap_detection_enabled" => 1,
        "process_perf_data" => 1,
        "retain_status_information" => 1,
        "retain_nonstatus_information" => 1,
        "notification_interval" => 0,
        "notification_period" => "24x7",
        "notification_options" => "a",
        "notifications_enabled" => 1,
        "register" => 0,
        "name" => "default-service"
      }]
    }
  end

  def add_obj(type, parameters)
    if not @current_config.has_key?(type) then
      @current_config[type] = []
    end
    @current_config[type].push(parameters)
  end

  def configfile
    res = ""
    @current_config.each do |type, objects|
      objects.each do |object|
        res += "define #{type} {\n"
        object.each do |key, value|
          res += sprintf "     %-30s%s\n", key, value
        end
        res +="}\n\n";
      end
    end
    res
  end
end

Before do
  @naemonconfig = NaemonObjectConfig.new
end