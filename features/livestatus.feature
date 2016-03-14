@config @daemons @merlin @queryhandler @livestatus
Feature: I can ask livestatus for information
	Those steps is just to verify the test environment livestatus steps, rather
	than verifying merlin. Should be removed when livestatus requests is added
	to merlin tests.

Background: Start naemon
	Given I have naemon host objects
		| use          | host_name | address   |
		| default-host | something | 127.0.0.1 |
		| default-host | gurka     | 127.0.0.2 |
	Given I have merlin configured for port 7000
		| type | name   | port |
	And I start naemon

Scenario: Ask livestatus
	When I submit the following livestatus query
		| GET hosts             |
		| Columns: name address |
	Then I should see the following livestatus response
		| name      | address   |
		| gurka     | 127.0.0.2 |
		| something | 127.0.0.1 |
