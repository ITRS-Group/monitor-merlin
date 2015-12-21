# Before, so the variable is instanced in the scenario class
Before do
  @merlin_packet_defaults = {
    "CTRL_ACTIVE" => {
      "version" => "1",
      "word_size" => "64",
      "byte_order" => "1234",
      "object_structure_version" => "402",
      "start" => "1446586100.291601",
      "last_cfg_change" => "17",
      "config_hash" => "my_hash",
      "peer_id" => "0",
      "active_peers" => "0",
      "configured_peers" => "0",
      "active_pollers" => "0",
      "configured_pollers" => "0",
      "active_masters" => "0",
      "configured_masters" => "0",
      "host_checks_handled" => "4",
      "service_checks_handled" => "92",
      "monitored_object_state_size" => "408"
    }
  }
end