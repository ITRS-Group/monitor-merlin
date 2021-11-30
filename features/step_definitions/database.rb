require "sequel"

Given(/^I have a database running$/) do
    h = @databaseconfig
    args = "--log-error=./db.log --general-log=./general-db.log --datadir=./db/data --socket=./db.sock --pid-file=./db.pid"
    # The reason we use the exact path to mysqld here is because it's not in $PATH. The
    # alternative is to use mysqld_safe which is in $PATH but it doesn't terminate when
    # we try to kill it with SIGTERM
    steps %Q{
      And I start command mysql_install_db --auth-root-authentication-method=normal #{args}
      And I start daemon /usr/libexec/mysqld --user=root #{args} --port=#{h.port}
      And I wait for 5 second
    }
    Sequel.mysql2(:host => h.host, :port => h.port, :username => "root") do |db|
        db.run "CREATE USER '#{h.user}'@'#{h.host}' IDENTIFIED BY '#{h.pass}'"
        db.run "CREATE DATABASE #{h.name}"
        db.run "GRANT ALL PRIVILEGES ON #{h.name}.* To '#{h.user}'@'#{h.host}' IDENTIFIED BY '#{h.pass}'"
    end
    Sequel.mysql2(:host => h.host, :port => h.port, :username => h.user, :password => h.pass, :database => h.name) do |db|
        db.run "CREATE TABLE notification( \
                    instance_id         int NOT NULL DEFAULT 0, \
                    id                  int NOT NULL PRIMARY KEY AUTO_INCREMENT, \
                    notification_type   int, \
                    start_time          int(11), \
                    end_time            int(11), \
                    contact_name        varchar(255), \
                    host_name           varchar(255), \
                    service_description varchar(255), \
                    command_name        varchar(255), \
                    reason_type         int, \
                    state               int, \
                    output              text, \
                    ack_author          varchar(255), \
                    ack_data            text, \
                    escalated           int, \
                    contacts_notified   int \
               ) COLLATE latin1_general_cs"
        
        db.run "CREATE TABLE report_data( \
                    id                  bigint NOT NULL AUTO_INCREMENT PRIMARY KEY, \
                    timestamp           int(11) NOT NULL DEFAULT '0', \
                    event_type          int(11) NOT NULL DEFAULT '0', \
                    flags               int(11) DEFAULT NULL, \
                    attrib              int(11) DEFAULT NULL, \
                    host_name           varchar(255) COLLATE latin1_general_cs DEFAULT '', \
                    service_description varchar(255) COLLATE latin1_general_cs DEFAULT '', \
                    state               int(2) NOT NULL DEFAULT '0', \
                    hard                int(2) NOT NULL DEFAULT '0', \
                    retry               int(5) NOT NULL DEFAULT '0', \
                    downtime_depth      int(11) DEFAULT NULL, \
                    output              text COLLATE latin1_general_cs, \
                    long_output         text COLLATE latin1_general_cs, \
                    KEY rd_timestamp    (`timestamp`), \
                    KEY rd_event_type   (`event_type`), \
                    KEY rd_name_evt_time(`host_name`,`service_description`,`event_type`,`hard`,`timestamp`), \
                    KEY rd_state        (`state`) \
               ) COLLATE latin1_general_cs"
    end
end

Given(/^CONTACT_NOTIFICATION_METHOD is logged in the database (\d+) times? with data$/) do |times, values|
    h = @databaseconfig
    v = values.rows_hash
    Sequel.mysql2(:host => h.host, :port => h.port, :username => h.user, :password => h.pass, :database => h.name) do |db|
        db_times = db[:notification].where(
            :contact_name => v.fetch("contact_name", nil),
            :host_name => v.fetch("host_name", nil),
            :service_description => v.fetch("service_description", nil),
            :command_name => v.fetch("command_name", nil),
            :output => v.fetch("output", nil),
            :ack_author => v.fetch("ack_author", nil),
            :ack_data => v.fetch("ack_data", nil)).count
        if db_times != times.to_i
            fail("found #{db_times} line(s) in database but expected #{times} line(s)")
        end
    end
end

Given(/^([a-zA-Z_\d]+) contains? (\d+) matching rows?$/) do |table, times, values|
    values.map_headers! {|key| key.downcase.to_sym } # Symbolize keys
    h = @databaseconfig
    Sequel.mysql2(:host => h.host, :port => h.port, :username => h.user, :password => h.pass, :database => h.name) do |db|
        db_times = db[:report_data].where(values.rows_hash).count
        if db_times != times.to_i
          fail("found #{db_times} line(s) in database but expected #{times} line(s)")
        end
    end
end

Given(/^([a-zA-Z_\d]+) has (\d+) entries?$/) do |table, num|
    h = @databaseconfig
    Sequel.mysql2(:host => h.host, :port => h.port, :username => h.user, :password => h.pass, :database => h.name) do |db|
        db_times = db[:report_data].count
        if db_times != num.to_i
            fail("#{table} has #{db_times} entries, expected #{num}")
        end
    end
end
