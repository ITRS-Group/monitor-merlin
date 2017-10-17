Given(/^I send naemon command (.*)$/) do |command|
  full_command = "[0] "+command
  step "I ask query handler command run #{full_command}", Cucumber::Ast::Table.new([
    ["filter_var", "filter_val", "match_var", "match_val"]
  ])
end

Given(/^naemon status ([^ ]+) should be set to (.*)$/) do |name, value|
  steps %Q{
    When I submit the following livestatus query
      | GET status       |
      | Columns: #{name} |
    Then I should see the following livestatus response
      | #{name}  |
      | #{value} |
  }
end

Then(/^(.*) of service (.*) on host (.*) should be (.*)$/) do
    | param, service, host, value |
  steps %Q{
     When I submit the following livestatus query
       | GET services                            |
       | Columns: #{param} description host_name |
       | Filter: description = #{service}        |
       | Filter: host_name = #{host}             |
     Then I should see the following livestatus response
       | #{param} | description | host_name |
       | #{value} | #{service}  | #{host}   |
    }
end

Then(/^(.*) of host (.*) should be (.*)$/) do
    | param, host, value |
  steps %Q{
     When I submit the following livestatus query
       | GET hosts              |
       | Columns: #{param} name |
       | Filter: name = #{host} |
     Then I should see the following livestatus response
       | #{param} | name    |
       | #{value} | #{host} |
    }
end

Then(/^host (.*) should be in downtime$/) do |host|
  steps %Q{
     When I submit the following livestatus query
       | GET hosts                            |
       | Columns: name                        |
       | Filter: name = #{host}               |
       | Filter: scheduled_downtime_depth > 0 |
     Then I should see the following livestatus response
       | name    |
       | #{host} |
    }
end

When(/^I put host (.*) in downtime for (.*) seconds?$/) do | host, duration |
  start = Time.now.to_i
  stop = start + duration.to_i
  steps %Q{
     When I send naemon command SCHEDULE_HOST_DOWNTIME;hostA;#{start};#{stop};1;0;#{duration.to_i};Test;Test
     And I wait for 1 second
     Then host #{host} should be in downtime
    }
end

# Generate a regex pattern from the supplied table to search the the
# notifications log.
Then(/^(\d+) (host|service) notifications? (?:was|were) sent$/) do | count, type, table |
  regex = "^notif #{type}" # Log entries always start with notif service/host
  table.hashes.each do |obj|
    # Append each parameter and value given in the table to the regex pattern
    # string. We use positive lookahead so we don't have to consider order
    # when later trying to match.
    regex << "(?=.* #{obj["parameter"]}=#{obj["value"]})"
  end
  regex << ".*$"
  step "file notifications.log has #{count} line matching #{regex}"
end

Then(/^no (host|service) notification (?:was|has been) sent$/) do | type |
  step "file notifications.log does not match ^notif #{type}"
end

When(/^I send a passive check result for service (.*) on host (.*) with (\d+) MiB output data?$/) do | service, host, size |
  # 65171 * 8 corresponds to 1 MiB (1048576 bytes)
  output = "." * 65171
  repeats = 8 * size.to_i

  steps %Q{
     When for #{repeats} times I send naemon command PROCESS_SERVICE_CHECK_RESULT;#{host};#{service};0;#{output}
    }
end

Then(/^the (.*) should not be larger than (\d+) MiB$/) do | binlog, size |
  size = size.to_i * 1024 * 1024

  steps %Q{
     Then the file #{binlog} should not be larger than #{size} bytes
    }
end
