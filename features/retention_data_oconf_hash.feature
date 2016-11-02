@config @daemons @queryhandler @oconf @livestatus
Feature: Retention data should not affect config hash
	Since we calculate a hash of the current configuration to check if the
	configuration should be synced, only the parts that is synced should affect
	the hash.
	
	Scenario: The hash is reported correctly in a clean system
		Given I have naemon hostgroup objects
			| hostgroup_name | alias             |
			| poller-group   | Poller host group |
		And I have naemon host objects
			| use          | host_name | address   | hostgroups   | active_checks_enabled |
			| default-host | something | 127.0.0.1 | poller-group | 1                     |
		And I have merlin configured for port 7000
			| type   | name     | port | hostgroup    |
			| poller | myPoller | 4001 | poller-group |
		And I have config file status.sav
			"""
			host {
			host_name=something
			modified_attributes=2
			config:active_checks_enabled=1
			active_checks_enabled=0
			}
			"""
		When I start naemon
		And I wait for 2 seconds
		And I submit the following livestatus query
			| GET hosts                                   |
			| Columns: name address active_checks_enabled |
		Then I should see the following livestatus response
			| name      | address   | active_checks_enabled |
			| something | 127.0.0.1 | 0                     |

		But file myPoller.cfg matches active_checks_enabled[\s]*1