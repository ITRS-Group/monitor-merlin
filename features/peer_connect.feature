@merlin @daemons
Feature: Merlind connects to peer
	Verify that merlind connects to a peer

	Also verify that the hash from module passed though ipc is forwarded
	correctly

	Background: Set up merlind connecting to peer01, mock ipc
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
				port = 4001 # 3001+7000 = 11001
				address = 127.0.0.1
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
		Then ipc is connected to merlin

	Scenario: Start listen to peer01, and get a connection
		Given peer01 listens for merlin at port 4001
		# Merlin reconnects every 5 seconds, have some margin.
		And I wait for 6 seconds
		Then peer01 is connected to merlin
		Then peer01 received event CTRL_ACTIVE
			| last_cfg_change             | 17                |
			| config_hash                 | my_hash           |