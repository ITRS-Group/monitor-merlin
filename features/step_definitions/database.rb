Given(/^I have a database running configured with$/) do |config|
    h = config.rows_hash
    host, port, user, pass, name = h["host"], h["port"], h["user"], h["pass"], h["name"]
    args = "--log-error=./db.log --datadir=./db/data --socket=./db.sock --pid-file=./db.pid"
    steps %Q{
      And I start command mysql_install_db #{args}
      And I wait for 1 second
      And I start daemon /usr/libexec/mysqld #{args} --port=#{port}
      And I start command mysql --protocol=TCP --port=#{port} --user root \
        --execute=\"CREATE USER '#{user}'@'#{host}' IDENTIFIED BY '#{pass}';\"
      And I start command mysql --protocol=TCP --port=#{port} --user root \
        --execute=\"CREATE DATABASE #{name};\"
      And I start command mysql --protocol=TCP --port=#{port} --user root \
        --execute=\"GRANT ALL PRIVILEGES ON #{name}.* To '#{user}'@'#{host}' IDENTIFIED BY '#{pass}';\"
    }
end
