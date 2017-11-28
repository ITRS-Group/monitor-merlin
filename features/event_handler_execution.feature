@config @daemons @merlin @queryhandler @livestatus
Feature: Merlin distributes event handlers the same way it distributes check
    executions. This will ensure an event is only handled at one node.

	Background: Set up naemon configuration

		Given I have naemon hostgroup objects
			| hostgroup_name | alias |
			| pollergroup    | PG    |
			| emptygroup     | EG    |
		And I have naemon host objects
			| use          | host_name | address   | contacts  | max_check_attempts | hostgroups  | event_handler |
			| default-host | hostA     | 127.0.0.1 | myContact | 2                  | pollergroup | event_handler |
			| default-host | hostB     | 127.0.0.2 | myContact | 2                  | pollergroup | event_handler |
		And I have naemon service objects
			| use             | host_name | description | event_handler |
			| default-service | hostA     | PONG        | event_handler |
			| default-service | hostB     | PONG        | event_handler |
		And I have naemon contact objects
			| use             | contact_name |
			| default-contact | myContact    |

    Scenario: As a peer I should only handle events for hosts I am responsible for

        Given I start naemon with merlin nodes connected
            | type   | name       | port |
            | peer   | the_peer   | 4001 |

        When the_peer sends event HOST_CHECK
            | name                  | hostA |
            | state.state_type      | 1     |
            | state.current_state   | 1     |
            | state.current_attempt | 1     |
        And the_peer sends event HOST_CHECK
            | name                  | hostB |
            | state.state_type      | 1     |
            | state.current_state   | 1     |
            | state.current_attempt | 1     |

        Then 0 events were handled for host hostA
        And 1 event was handled for host hostB
    
    Scenario: As a peer I should only handle events for services I am responsible for

        Given I start naemon with merlin nodes connected
            | type   | name       | port |
            | peer   | the_peer   | 4001 |

        When the_peer sends event SERVICE_CHECK
            | host_name             | hostA |
            | service_description   | PONG  |
            | state.state_type      | 1     |
            | state.current_state   | 1     |
            | state.current_attempt | 1     |
        And the_peer sends event SERVICE_CHECK
            | host_name             | hostB |
            | service_description   | PONG  |
            | state.state_type      | 1     |
            | state.current_state   | 1     |
            | state.current_attempt | 1     |

        Then 0 events were handled for service PONG on host hostA
        And 1 event was handled for service PONG on host hostB

	Scenario: As a poller I should only handle events for hosts I am responsible for
	
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroup   |
			| master | the_master | 4001 | ignore      |
			| peer   | the_peer   | 4002 | pollergroup |

        When the_peer sends event HOST_CHECK
            | name                  | hostA |
            | state.state_type      | 1     |
            | state.current_state   | 1     |
            | state.current_attempt | 1     |
        And the_peer sends event HOST_CHECK
            | name                  | hostB |
            | state.state_type      | 1     |
            | state.current_state   | 1     |
            | state.current_attempt | 1     |

        Then 0 events were handled for host hostA
        And 1 event was handled for host hostB

    Scenario: As a poller I should only handle events for services I am responsible for

        Given I start naemon with merlin nodes connected
            | type   | name       | port | hostgroup   |
            | master | the_master | 4001 | ignore      |
            | peer   | the_peer   | 4002 | pollergroup |

        When the_peer sends event SERVICE_CHECK
            | host_name             | hostA |
            | service_description   | PONG  |
            | state.state_type      | 1     |
            | state.current_state   | 1     |
            | state.current_attempt | 1     |
        And the_peer sends event SERVICE_CHECK
            | host_name             | hostB |
            | service_description   | PONG  |
            | state.state_type      | 1     |
            | state.current_state   | 1     |
            | state.current_attempt | 1     |

        Then 0 events were handled for service PONG on host hostA
        And 1 event was handled for service PONG on host hostB