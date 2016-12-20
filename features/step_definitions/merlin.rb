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

Then(/^([a-z0-9\-_]+) received notification after all host check results$/) do |n|
  log_file = "merlin.log"
  step "file #{log_file} has no match type 7 to node #{n} after match 5 to node #{n}"
end

Then(/^([a-z0-9\-_]+) received notification after all service check results$/) do |n|
  log_file = "merlin.log"
  step "file #{log_file} has no match type 6 to node #{n} after match 5 to node #{n}"
end