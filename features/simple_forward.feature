@merlin @daemons @config
Feature: Simple packet forwarding
	Verify the behaviour of having one peer, and one IPC, that packets sent
	between connections should be forwarded to other connections

	Also verify that packets doesn't bounce back to the source

	Background: Set up merlind with peer01 and ipc
		Given I have merlin configured for port 7000
			| type | name   | port | connect |
			| peer | peer01 | 4001 | no      |

		And I start merlin
		And I wait for 1 second

		And ipc connect to merlin at socket test_ipc.sock
		And ipc sends event CTRL_ACTIVE
			| configured_peers   | 1 |
			| configured_pollers | 0 |

			# Make sure it knows about the config hash
		And I wait for 1 second
		Then ipc is connected to merlin

		Given peer01 connect to merlin at port 7000 from port 11001
		And peer01 sends event CTRL_ACTIVE
			| configured_peers   | 1 |
			| configured_pollers | 0 |
		#this is the default during test
		And I wait for 1 second
		Then peer01 is connected to merlin

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
