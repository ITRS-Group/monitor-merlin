Given(/^I have naemon (.+) objects$/) do |type, table|
  table.hashes.each do |obj|
    @naemonconfig.add_obj(type, obj)
  end
end

Given(/^I have naemon objects stored in (.+)$/) do |filename|
  step "I have config file #{filename}", @naemonconfig.configfile
end