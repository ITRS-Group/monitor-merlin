@config @daemons @merlin @queryhandler @livestatus
Feature: Report data is logged from certain active host check results

    Background: Set up naemon configuration
        Given I have naemon host objects
            | use          | host_name | address   | max_check_attempts | active_checks_enabled | check_interval | check_command         | initial_state |
            | default-host | hostA     | 127.0.0.1 | 3                  |                     1 |              1 | check_active_critical | o             |
            | default-host | hostB     | 127.0.0.1 | 3                  |                     1 |              1 | check_active_ok       | d             |
        And I have naemon config interval_length set to 1
        And I have naemon config cached_host_check_horizon set to 0
        And I have naemon config execute_host_checks set to 0
        
    Scenario: A host is initially in hard up state and goes down
        Given I start naemon with merlin nodes connected
            | type   | name   | port | hostgroup    |
            
        When I send naemon command START_EXECUTING_HOST_CHECKS
        
        Then ipc received event HOST_CHECK
            | nebattr                        | 2                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 0                  |
            | state.current_attempt          | 1                  |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostA              |
        And ipc received event HOST_CHECK
            | nebattr                        | 0                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 0                  |
            | state.current_attempt          | 2                  |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostA              |
        And ipc received event HOST_CHECK
            | nebattr                        | 1                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 1                  |
            | state.current_attempt          | 3                  |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostA              |
        And ipc received event HOST_CHECK
            | nebattr                        | 0                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 1                  |
            | state.current_attempt          | 1                  |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostA              |
            
    Scenario: A host is initially in hard down state and comes back up
        Given I start naemon with merlin nodes connected
            | type   | name   | port | hostgroup    | 
            
        When I send naemon command START_EXECUTING_HOST_CHECKS
        
        Then ipc received event HOST_CHECK
            | nebattr                        | 3                  |
            | state.check_type               | 0                  |
            | state.current_state            | 0                  |
            | state.state_type               | 1                  |
            | state.current_attempt          | 1                  |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostB              |
            
    @unreliable_el8
    Scenario: Report data is logged when a host is initially up, then goes down and then back up again.
        Given I start naemon with merlin nodes connected
            | type   | name   | port | hostgroup    |
        And I start merlin
        And ipc connect to merlin at socket test_ipc.sock
        And ipc is connected to merlin
        
        When ipc sends event HOST_CHECK
            | nebattr                        | 2                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 0                  |
            | state.current_attempt          | 1                  |
            | state.last_check               | 101                |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostA              |
        And ipc sends event HOST_CHECK
            | nebattr                        | 0                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 0                  |
            | state.current_attempt          | 2                  |
            | state.last_check               | 102                |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostA              |
        And ipc sends event HOST_CHECK
            | nebattr                        | 1                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 1                  |
            | state.current_attempt          | 3                  |
            | state.last_check               | 103                |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostA              |
        And ipc sends event HOST_CHECK
            | nebattr                        | 0                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 1                  |
            | state.current_attempt          | 1                  |
            | state.last_check               | 104                |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostA              |
        And ipc sends event HOST_CHECK
            | nebattr                        | 3                  |
            | state.check_type               | 0                  |
            | state.current_state            | 0                  |
            | state.state_type               | 1                  |
            | state.current_attempt          | 1                  |
            | state.last_check               | 105                |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostA              |
        And I wait for 3 seconds

        Then report_data contain 1 matching row
            | timestamp      | 101                |
            | event_type     | 801                |
            | host_name      | hostA              |
            | state          | 1                  |
            | hard           | 0                  |
            | retry          | 1                  |
            | output         | Plugin output      |
            | long_output    | Long plugin output |
            | downtime_depth | 0                  |
        And report_data contain 1 matching row
            | timestamp      | 103                |
            | event_type     | 801                |
            | host_name      | hostA              |
            | state          | 1                  |
            | hard           | 1                  |
            | retry          | 3                  |
            | output         | Plugin output      |
            | long_output    | Long plugin output |
            | downtime_depth | 0                  |
        And report_data contain 1 matching row
            | timestamp      | 105                |
            | event_type     | 801                |
            | host_name      | hostA              |
            | state          | 0                  |
            | hard           | 1                  |
            | retry          | 1                  |
            | output         | Plugin output      |
            | long_output    | Long plugin output |
            | downtime_depth | 0                  |
        And report_data has 3 entries
            
    @unreliable_el8
    Scenario: Report data is logged when a host is initially down, then goes up and then back down again.
        Given I start naemon with merlin nodes connected
            | type   | name   | port | hostgroup    |
        And I start merlin
        And ipc connect to merlin at socket test_ipc.sock
        And ipc is connected to merlin
        
        When ipc sends event HOST_CHECK
            | nebattr                        | 3                  |
            | state.check_type               | 0                  |
            | state.current_state            | 0                  |
            | state.state_type               | 1                  |
            | state.current_attempt          | 1                  |
            | state.last_check               | 101                |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostB              |
        And ipc sends event HOST_CHECK
            | nebattr                        | 2                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 0                  |
            | state.current_attempt          | 1                  |
            | state.last_check               | 102                |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostB              |
        And ipc sends event HOST_CHECK
            | nebattr                        | 0                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 0                  |
            | state.current_attempt          | 2                  |
            | state.last_check               | 103                |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostB              |
        And ipc sends event HOST_CHECK
            | nebattr                        | 1                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 1                  |
            | state.current_attempt          | 3                  |
            | state.last_check               | 104                |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostB              |
        And ipc sends event HOST_CHECK
            | nebattr                        | 0                  |
            | state.check_type               | 0                  |
            | state.current_state            | 1                  |
            | state.state_type               | 1                  |
            | state.current_attempt          | 1                  |
            | state.last_check               | 105                |
            | state.scheduled_downtime_depth | 0                  |
            | state.plugin_output            | Plugin output      |
            | state.long_plugin_output       | Long plugin output |
            | state.perf_data                | Performance data   |
            | name                           | hostB              |
        And I wait for 3 seconds

        Then report_data contain 1 matching row
            | timestamp      | 101                |
            | event_type     | 801                |
            | host_name      | hostB              |
            | state          | 0                  |
            | hard           | 1                  |
            | retry          | 1                  |
            | output         | Plugin output      |
            | long_output    | Long plugin output |
            | downtime_depth | 0                  |
        And report_data contain 1 matching row
            | timestamp      | 102                |
            | event_type     | 801                |
            | host_name      | hostB              |
            | state          | 1                  |
            | hard           | 0                  |
            | retry          | 1                  |
            | output         | Plugin output      |
            | long_output    | Long plugin output |
            | downtime_depth | 0                  |
        And report_data contain 1 matching row
            | timestamp      | 104                |
            | event_type     | 801                |
            | host_name      | hostB              |
            | state          | 1                  |
            | hard           | 1                  |
            | retry          | 3                  |
            | output         | Plugin output      |
            | long_output    | Long plugin output |
            | downtime_depth | 0                  |
        And report_data has 3 entries
