Given(/^([a-z0-9\-_]+) sends event ([A-Z_]+)$/) do |ref, event, values|
  defaults = {}
  if @merlin_packet_defaults.has_key? event then
    defaults = @merlin_packet_defaults[event].clone
  end

  # Allow multiple objects to be send
  values.transpose.hashes.each do |in_obj|
    # merge with default values and repack
    in_obj = defaults.merge(in_obj)
    step "#{ref} sends raw event #{event}", Cucumber::Ast::Table.new(in_obj.to_a)
  end
end

Then(/^(\d*) notifications? for host (.*) was sent after check result$/) do |count,h|
  steps %Q{
    Then file merlin.log has #{count} line matching holding host notification for #{h}$
    And file merlin.log has #{count} line matching flushing host notification for #{h}$
  }
end

Then(/^(\d*) notifications? for service (.*) on host (.*) was sent after check result$/) do |count,s,h|
  steps %Q{
  Then file merlin.log has #{count} line matching holding service notification for #{s};#{h}$
    And file merlin.log has #{count} line matching flushing service notification for #{s};#{h}$
  }
end

Then(/^no notification was held$/) do
  step "file merlin.log does not match holding.*notification"
end
Then(/([a-z0-9\-_]+) (?:should appear|appears) disconnected$/) do |n|
  steps %Q{
    Given I ask query handler merlin nodeinfo
      | filter_var | filter_val | match_var | match_val |
      | name | #{n} | state | STATE_NONE |
  }
end

Then(/([a-z0-9\-_]+) (?:should appear|appears) connected$/) do |n|
  steps %Q{
    Given I ask query handler merlin nodeinfo
      | filter_var | filter_val | match_var | match_val |
      | name | #{n} | state | STATE_CONNECTED |
  }
end

Then(/([a-z0-9\-_]+) (?:should become|becomes) disconnected/) do |n|
  steps %Q{
    Then #{n} disconnects from merlin
    And #{n} appears disconnected
  }
end