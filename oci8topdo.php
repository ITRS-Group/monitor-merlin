<?php

class OciException extends PdoException {}

class Oci8ToPdo extends PDO {
	function __construct($host, $name, $user, $password, $port=false) {
		$this->link = oci_connect($user, $password, "//$host".($port?":$port":'')."/$name");

		if (!$this->link) {
			$this->throw_exception();
		}
	}

	private function throw_exception() {
		$e = oci_error();
		if ($e) {
			throw new OciException($e['message']);
		} else {
			throw new OciException('Unknown error');
		}
	}

	function query($sql) {
		// Rewrite LIMIT/OFFSET to oracle compatible thingies
		$matches = false;
		if (preg_match('/(.*) LIMIT (\d+)( OFFSET (\d+))?$/s', $sql, $matches)) {
			$sql = trim($matches[1]);
			$offset = isset($matches[4]) ? $matches[4] : 0;
			$limit = $matches[2] + $offset;
			if ($limit) {
				$sql = "SELECT foo.*, rownum AS rnum FROM ({$matches[1]}) foo WHERE rownum <= $limit";
				if ($offset)
					$sql = "SELECT bar.* FROM ($sql) bar WHERE rnum > $offset";
			}
		}
		$sql = str_replace('NOW()', 'SYSDATE', $sql);
		// Rewrite UNIX_TIMESTAMP
		$sql = str_replace('UNIX_TIMESTAMP()', "((sysdate - to_date('01-JAN-1970', 'DD-MON-YYYY')) * 86400)", $sql);
		$sql = preg_replace('/UNIX_TIMESTAMP\(([^)]+)\)/', '((${1} - to_date(\'01-JAN-1970\', \'DD-MON-YYYY\')) * 86400)', $sql);

		// LCASE is called LOWER
		$sql = str_replace('LCASE', 'LOWER', $sql);
		
		$res = new Oci8ToPdo_Result($this->link, $sql);
		$res->execute();
		return $res;
	}

	function quote($str, $unused=null) {
		if (strlen($str) >= 4000) {
			throw new OciException("Danger! You tried to quote a string that's too long for Oracle. You need to rewrite your query! Field: $str");
		}
		if (strtolower($str) == 'true' || (strtolower($str) == 'false'))
			return strtolower($str) == 'true' ? 'TRUE' : 'FALSE';
		if (strtolower($str) == 'null')
			return 'NULL';
		if (is_numeric($str))
			return (int)$str;
		return "'".str_replace("'", "''", $str)."'";
	}

	function prepare($sql, $unused=null) {
		return new Oci8ToPdo_Result($this->link, $sql);
	}
}

class Oci8ToPdo_Result implements Countable {
	public function fetch($type=PDO::FETCH_BOTH, $unused=null, $unused=null)
	{
		$res = false;
		$latest_row = @oci_fetch_assoc($this->result);
		$this->current_row++;
		$obj = new StdClass();
		if ($latest_row) {
			foreach($latest_row as $key => $var) {
				$name = strtolower($key);
				if (is_object($var)) {
					$val = $var->load();
					$var->close();
					$var = $val;
				}
				$obj->$name = $var;
			}
			// No, PDO::FETCH_$foo is not bitmask values
			if ($type == PDO::FETCH_NUM || $type == PDO::FETCH_BOTH) {
				foreach (get_object_vars($obj) as $val)
					$res[] = $val;
				if ($type == PDO::FETCH_BOTH)
					$res = array_merge($res, get_object_vars($obj));
			} elseif ($type == PDO::FETCH_ASSOC) {
				$res = get_object_vars($obj);
			} else {
				$res = $obj;
			}
		}

		if ($e = oci_error()) {
			debug_print_backtrace();
			throw new OciException($e['message']);
		}
		return $res;
	}

	public function fetchAll($style=PDO::FETCH_BOTH, $unused=null, $unused=null)
	{
		$res = array();
		while ($this->valid()) {
			$res[] = $this->fetch($style);
		}
		return $res;
	}

	public function fetchColumn($which)
	{
		$res = $this->fetch(PDO::FETCH_NUM);
		return $res[$which];
	}

	public function count()
	{
		return ($this->total_rows);
	}

	public function valid()
	{
		return ($this->current_row < $this->total_rows);
	}

	protected function pdo_row_count()
	{
		$count = 0;
		while (oci_fetch_assoc($this->result)) {
			$count++;
		}

		// The query must be re-fetched now.
		oci_execute($this->result, OCI_COMMIT_ON_SUCCESS);
		return $count;
	}

	function __construct($link, $sql)
	{
		$this->link = $link;
		$this->result = oci_parse($link, $sql);
		$this->sql = $sql;
	}

	function execute($parameters=null)
	{
		if (!$this->result) {
			return false;
		} elseif ($err = oci_error()) {
			throw new OciException($err['message'].' - SQL=['.$sql.']');
		}

		if ($parameters != null && $this->result)
			foreach ($parameters as $key => $val) {
				oci_bind_by_name($this->result, $key, $parameters[$key]);
			}

		if (!@oci_execute($this->result, OCI_COMMIT_ON_SUCCESS)) {
			$e = oci_error($this->result);
			// code 923 means no FROM found
			// this workaround sometimes works
			if ($e['code'] == 923) {
				$sql .= "\nFROM DUAL";
				$this->result = oci_parse($this->link, $sql);
				if (!@oci_execute($this->result, OCI_COMMIT_ON_SUCCESS)) {
					debug_print_backtrace();
					throw new OciException($e['message'].' - SQL=['.$this->sql.']');
				}

			}
			else {
				throw new OciException($e['message'].' - SQL=['.$this->sql.']');
			}
		}

		if (preg_match('/^\s*(SHOW|DESCRIBE|SELECT|PRAGMA|EXPLAIN)/is', $this->sql)) {
			$this->current_row = 0;

			$this->total_rows = $this->pdo_row_count();

		} elseif (preg_match('/^\s*(DELETE|INSERT|UPDATE)/is', $this->sql)) {
			# completely broken, but I don't care
			$this->insert_id  = 0;
		}
		return true;
	}
}
