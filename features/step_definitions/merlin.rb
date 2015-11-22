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