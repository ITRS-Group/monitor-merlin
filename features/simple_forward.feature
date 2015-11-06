@merlin @daemons
Feature: Simple packet forwarding
	Verify the behaviour of having one peer, and one IPC, that packets sent
	between connections should be forwarded to other connections

	Also verify that packets doesn't bounce back to the source

	Background: Set up merlind with peer01 and ipc
		Given I have config file merlin.conf
			"""
			ipc_socket = /tmp/test_ipc.sock;

			log_level = info;
			use_syslog = 1;

			module {
				log_file = /dev/null
			}
			daemon {
				pidfile = /var/run/merlin/merlin.pid;
				log_file = /dev/stdout
				import_program = /bin/false
				port = 7000;
				object_config {
					dump = /bin/false
				}
			}

			peer peer01 {
				port = 4001 # 4001+7000 = 11001
				address = 127.0.0.1
				connect = no
			}
			"""

		And I start daemon merlind -d merlin.conf
		And I wait for 1 second

		And ipc connect to merlin at socket /tmp/test_ipc.sock
		And ipc sends event CTRL_ACTIVE
			| version                     | 1                 |
			| word_size                   | 64                |
			| byte_order                  | 1234              |
			| object_structure_version    | 402               |
			| start                       | 1446586100.291601 |
			| last_cfg_change             | 17                |
			| config_hash                 | my_hash           |
			| peer_id                     | 1                 |
			| active_peers                | 0                 |
			| configured_peers            | 1                 |
			| active_pollers              | 0                 |
			| configured_pollers          | 0                 |
			| active_masters              | 0                 |
			| configured_masters          | 0                 |
			| host_checks_handled         | 4                 |
			| service_checks_handled      | 92                |
			| monitored_object_state_size | 408               |

			# Make sure it knows about the config hash
		And I wait for 1 second

		And peer01 connect to merlin at port 7000 from port 11001
		And peer01 sends event CTRL_ACTIVE
			| version                     | 1                 |
			| word_size                   | 64                |
			| byte_order                  | 1234              |
			| object_structure_version    | 402               |
			| start                       | 1446586100.291601 |
			| last_cfg_change             | 17                |
			| config_hash                 | my_hash           |
			| peer_id                     | 0                 |
			| active_peers                | 0                 |
			| configured_peers            | 1                 |
			| active_pollers              | 0                 |
			| configured_pollers          | 0                 |
			| active_masters              | 0                 |
			| configured_masters          | 0                 |
			| host_checks_handled         | 4                 |
			| service_checks_handled      | 92                |
			| monitored_object_state_size | 408               |
		And I wait for 1 second

	Scenario: Service check result propagation from ipc to peer
		Given peer01 clears buffer
		And ipc sends event SERVICE_CHECK
			| state.plugin_output | Some output from a cucumber test |
			| host_name           | monitor                          |
			| service_description | Cron process                     |
		And I wait for 1 second
		Then peer01 received event SERVICE_CHECK
			| state.plugin_output | Some output from a cucumber test |
			| host_name           | monitor                          |
			| service_description | Cron process                     |
		And ipc should not receive SERVICE_CHECK
			| host_name           | monitor                          |
			| service_description | Cron process                     |

	Scenario: Host check result propagation from ipc to peer
		Given peer01 clears buffer
		And ipc sends event HOST_CHECK
			| state.plugin_output | Some output from a cucumber test |
			| name                | monitor                          |
		And I wait for 1 second
		Then peer01 received event HOST_CHECK
			| state.plugin_output | Some output from a cucumber test |
			| name                | monitor                          |
		And ipc should not receive HOST_CHECK
			| name                | monitor                          |

	Scenario: Service check result propagation from peer to ipc
		Given ipc clears buffer
		And peer01 sends event SERVICE_CHECK
			| state.plugin_output | Some output from a cucumber test |
			| host_name           | monitor                          |
			| service_description | Cron process                     |
		And I wait for 1 second
		Then ipc received event SERVICE_CHECK
			| state.plugin_output | Some output from a cucumber test |
			| host_name           | monitor                          |
			| service_description | Cron process                     |
		And peer01 should not receive SERVICE_CHECK
			| host_name           | monitor                          |
			| service_description | Cron process                     |

	Scenario: Host check result propagation from peer to ipc
		Given ipc clears buffer
		And peer01 sends event HOST_CHECK
			| state.plugin_output | Some output from a cucumber test |
			| name                | monitor                          |
		And I wait for 1 second
		Then ipc received event HOST_CHECK
			| state.plugin_output | Some output from a cucumber test |
			| name                | monitor                          |
		And peer01 should not receive HOST_CHECK
			| name                | monitor                          |
