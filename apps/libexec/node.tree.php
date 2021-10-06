#!/usr/bin/php
<?php

/*  Author: Philip Eklof <peklof@op5.com>
 *  v0.01 2013-02-06
 */

class NodeTree
{
	// $node carries the parsed data from mon node show
	private $node = array();
	// $map contains all the output rows+columns
	private $map = array();

	// x axis padding, you don't want to go below 3.
	public $map_prefix_padding_length = 4;
	// the character(s) used for padding
	public $map_prefix_padding_char = ' ';

	public function __construct()
	{
		set_error_handler(array($this, 'exception_error_handler'));
	}

	// php error handler using exceptions
	public function exception_error_handler($errno, $errstr, $errfile, $errline)
	{
		throw new ErrorException($errstr, $errno, 0, $errfile, $errline);
	}

	// collects exception trace; will fail in php <5.3
	public function exception_parse($e)
	{
		$ret = array();

		do
		{
			$trace = $e->getTrace();
			$func = $trace[0]['function'];
			$ret[] = array(
				'file' => $e->getFile(),
				'line' => $e->getLine(),
				'func' => $func,
				'msg' => $e->getMessage()
			);
		}
		while($e = $e->getPrevious());

		return $ret;
	}

	// prints exception trace and exits
	public function exception_die($e)
	{
		$trace = $this->exception_parse($e);
		foreach(array_reverse($trace) as $n => $a)
		{
			printf(
				"#%d %s@%s %s(): %s\n",
				$n, $a['file'], $a['line'], $a['func'], $a['msg']
			);
		}
		exit(1);
	}

	public function syntaxdie($code=1)
	{
		$text = <<<EOSYNTAX

Prints an ascii map of the node network.

EOSYNTAX;
		echo $text;
		exit($code);
	}

	// finds the system binary path
	private function get_system_path()
	{
		if(!isset($_SERVER['PATH']))
			throw new Exception('SYSTEM_PATH_NOT_DEFINED');

		return explode(':', $_SERVER['PATH']);
	}

	// finds the filesystem path to a binary using the system path
	private function get_binary_path($bin)
	{
		if(!isset($bin))
			throw new Exception('MISSING_PARAM:bin');

		try {

			$system_path = $this->get_system_path();

			foreach($system_path as $dir)
			{
				if(is_executable("$dir/$bin"))
					return "$dir/$bin";
			}

		} catch(Exception $e) {
			throw new Exception('FAILED', 0, $e);
		}

		return false;
	}

	// execute system command and return output data
	public function execute($bin, $arg=NULL)
	{
		$bin_path = $this->get_binary_path($bin);
		if($bin_path === false)
			throw new Exception("BINARY_NOT_FOUND:$bin");

		if($arg === NULL)
			$cmd = $bin_path;
		else
			$cmd = $bin_path . ' ' . $arg;

		exec($cmd, $output, $ret);

		if($ret > 0)
			throw new Exception("FAILED_EXECUTING_CMD:$cmd");

		return $output;
	}

	// sorting function for usort(), longest string first
	private function sort_by_length($a, $b)
	{
		return strlen($b['NAME']) - strlen($a['NAME']);
	}

	// explodes text block into array of lines regardless of dos/mac/unix nls
	private function split_newlines($text)
	{
		if(!isset($text))
			throw new Exception('MISSING_PARAM:text');

		return preg_split("/((\r?\n)|(\r\n?))/", $text);
	}

	// parses the information about the nodes from mon node show
	public function nodes_parse_show($lines)
	{
		if(!isset($lines))
			throw new Exception('MISSING_PARAM:lines');

		if(!isset($this->node))
			throw new Exception('NOT_DEFINED:node');
		if(!is_array($this->node))
			throw new Exception('NOT_AN_ARRAY:node');

		// make sure input lines is an array of lines
		if(is_string($lines))
			$lines = $this->split_newlines($lines);
		elseif(!is_array($lines))
			throw new Exception('NOT_AN_ARRAY:lines');

		// add extra line to make sure the parsing includes the last node
		$lines[] = '';

		$go = false;
		foreach($lines as $line)
		{
			// collect data only when $go === true
			if($go === false)
			{
				// check for the "here-begins-new-host-comment" (e.g. # node)
				if(preg_match('/^#\s+\S+/', $line))
				{
					// parse for node preferences next run
					$go = true;
					$current = array();
				}
				continue;
			}

			// check if the line looks like a node preference
			if(preg_match('/^([A-Z_]+)=(.*)$/', $line, $match))
			{
				$var = $match[1];
				$val = $match[2];

				$current[$var] = $val;

				continue; // continue parsing preferences => try next line
			}
			else // seems like we've collected all preferences for this node
			{
				if(!isset($current['NAME']))
					throw new Exception('NODE_MISSING_NAME');
				if(!isset($current['TYPE']))
					throw new Exception('NODE_MISSING_TYPE');

				$type = $current['TYPE'];
				$name = $current['NAME'];

				// poller nodes are stored in their host group sub array
				if($type == 'poller')
				{
					if(!isset($current['HOSTGROUP']))
						throw new Exception("POLLER_MISSING_HOSTGROUP:$name");
					$group = $current['HOSTGROUP'];
				}
				else
				{
					$group = 0;
				}


				// create arrays if non existant
				if(!isset($this->node[$type]))
					$this->node[$type] = array();
				elseif(!is_array($this->node[$type]))
					$this->node[$type] = array();
				if(!isset($this->node[$type][$group]))
					$this->node[$type][$group] = array();
				elseif(!is_array($this->node[$type][$group]))
					$this->node[$type][$group] = array();

				// finally save node data
				$this->node[$type][$group][$name] = $current;

				$go = false;
				continue; // check for additional nodes
			}

		}

	}

	// sorts the array from nodes_parse() using sort_by_length()
	public function nodes_sort($type, $group)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');

		if(!isset($this->node[$type][$group]))
			throw new Exception("NOT_DEFINED:node:$type:$group");

		usort($this->node[$type][$group], array($this, 'sort_by_length'));
	}

	// returns array of all parsed node types (master, peer, poller...)
	public function get_node_types()
	{
		if(!isset($this->node))
			throw new Exception('NOT_DEFINED:node');
		if(!is_array($this->node))
			throw new Exception('NOT_AN_ARRAY:node');

		// make sure that node types master, peer and poller are prioritized
		$array_keys_prio = array();
		if(isset($this->node['master']))
			$array_keys_prio[] = 'master';
		if(isset($this->node['peer']))
			$array_keys_prio[] = 'peer';
		if(isset($this->node['poller']))
			$array_keys_prio[] = 'poller';

		// add all other node types to array
		$array_keys_all = array_keys($this->node);
		foreach($array_keys_all as $key)
		{
			switch($key)
			{
				case 'master':
				case 'peer':
				case 'poller':
					break;
				default:
					$array_keys_prio[] = $key;
			}
		}

		return $array_keys_prio;
	}

	// returns array of all groups of specified node type (default/hostgroups)
	public function get_node_groups($type)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');

		if(!isset($this->node[$type]))
			throw new Exception("NOT_DEFINED:node:$type");
		if(!is_array($this->node[$type]))
			throw new Exception("NOT_AN_ARRAY:node:$type");

		return array_keys($this->node[$type]);
	}

	// initialize the map for specified type+group with space for all columns
	public function map_init($type, $group)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');

		if(!isset($this->node[$type][$group]))
			throw new Exception("NOT_DEFINED:node:$type:$group");

		if(!isset($this->map))
			throw new Exception('NOT_DEFINED:map');
		if(!is_array($this->map))
			throw new Exception('NOT_AN_ARRAY:map');

		// create array for this node type if not already existing
		if(!isset($this->map[$type]) || !is_array($this->map[$type]))
			$this->map[$type] = array();

		// get the number of nodes
		$count = count($this->node[$type][$group]);
		// not necessarily a hostgroup, but that represents the longest header
		$header_name = "HOSTGROUP: $group";
		// element 0 contains the node with the longest name (if nodes_sort()'d)
		$box_name = $this->node[$type][$group][0]['NAME'];

		// get the x offset of the top box
		$map_val = $this->map_val_header($count, $header_name, $box_name);

		// three lines for every node + two separator lines
		$rows = 3 * $count + 2;
		// offset + size of box
		$cols = $map_val['x']['box'] + 2 + strlen($box_name) + 2;

		// create a two dimensional array for output, char by char
		$this->map[$type][$group] = array();
		for($row = 0; $row < $rows; $row++)
		{
			$this->map[$type][$group][$row] = array();
			for($col = 0; $col < $cols; $col++)
			{
				$this->map[$type][$group][$row][$col] = ' ';
			}
		}

		return true;
	}

	// strips whitespace columns at the end of the lines of the map
	public function map_strip($type, $group)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');

		if(!isset($this->map[$type][$group]))
			throw new Exception("NOT_DEFINED:node:$type:$group");

		$rows = count($this->map[$type][$group]);
		for($row = 0; $row < $rows; $row++)
		{
			$cols = count($this->map[$type][$group][$row]);
			for($col = ($cols-1); $col >= 0; $col--)
			{
				// unset element if space chr as long as we're at end of line
				if($this->map[$type][$group][$row][$col] == ' ')
					unset($this->map[$type][$group][$row][$col]);
				else
					break;
			}
		}

		return true;
	}

	// returns text block of specified map
	public function map_output($type, $group)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');

		if(!isset($this->map[$type][$group]))
			throw new Exception("NOT_DEFINED:map:$type:$group");

		$str = '';
		foreach($this->map[$type][$group] as $row)
		{
			foreach($row as $col)
				$str .= $col;

			$str .= "\n";
		}

		return $str;
	}

	// inserts a text string into the map at specified column+line
	private function map_insert_str($type, $group, $x, $y, $str)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');
		if(!isset($x))
			throw new Exception('MISSING_PARAM:x');
		if(!isset($y))
			throw new Exception('MISSING_PARAM:y');
		if(!isset($str))
			throw new Exception('MISSING_PARAM:str');

		if(!isset($this->map[$type][$group][$y]))
			throw new Exception("NOT_DEFINED:map:$type:$group:$y");

		if(!is_string($str))
			throw new Exception('PARAM_NOT_STRING:str');

		$chars = str_split($str);

		foreach($chars as $char)
		{
			$this->map[$type][$group][$y][$x] = $char;
			$x++;
		}
	}

	// inserts a text array (like box rows) into the map at specified column+line
	private function map_insert_box($type, $group, $x, $y, $rows)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');
		if(!isset($x))
			throw new Exception('MISSING_PARAM:x');
		if(!isset($y))
			throw new Exception('MISSING_PARAM:y');
		if(!isset($rows))
			throw new Exception('MISSING_PARAM:rows');

		if(!is_array($rows))
			throw new Exception('PARAM_NOT_ARRAY:rows');


		try {

			foreach($rows as $row)
			{
				$this->map_insert_str($type, $group, $x, $y, $row);
				$y++;
			}

		} catch(Exception $e) {
			throw new Exception('FAILED', 0, $e);
		}

	}

	// inserts a branch starting at specified column (always starts at row 2)
	private function map_insert_branch($type, $group, $x, $rows)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');
		if(!isset($x))
			throw new Exception('MISSING_PARAM:x');
		if(!isset($rows))
			throw new Exception('MISSING_PARAM:rows');

		if(!isset($this->map[$type][$group]))
			throw new Exception("NOT_DEFINED:map:$type:$group");

		$y = 2;

		for($row = 0; $row < $rows; $row++) // always begins on third line
		{
			$this->map[$type][$group][$y][$x] = "\\";
			$x++;
			$y++;
		}
	}


	// returns the prefix of correct length with specified string at the end
	private function map_get_prefix($str)
	{
		if(!isset($str))
			throw new Exception('MISSING_PARAM:str');

		$repstr = str_repeat(
			$this->map_prefix_padding_char,
			$this->map_prefix_padding_length
			);

		return $repstr . $str;
	}

	// returns array that represents a box with the specified name
	private function map_get_box($name)
	{
		if(!isset($name))
			throw new Exception('MISSING_PARAM:name');

		$name_len = strlen($name);
		$box = array();

		$toppom = '';
		for($n = 0; $n < $name_len+2; $n++)
			$toppom .= '-';
		$toppom = '+' . $toppom .'+';

		$box[] = $toppom;
		$box[] = "| $name |";
		$box[] = $toppom;

		return $box;
	}

	// returns all column and length values needed for specified header box
	private function map_val_header($count, $header_name, $box_name)
	{
		if(!isset($count))
			throw new Exception('MISSING_PARAM:count');
		if(!isset($header_name))
			throw new Exception('MISSING_PARAM:header_name');
		if(!isset($box_name))
			throw new Exception('MISSING_PARAM:box_name');

		if(!isset($this->map_prefix_padding_length))
			throw new Exception("NOT_DEFINED:map_prefix_padding_length");
		$len_prefix = $this->map_prefix_padding_length + 1;

		$val = array('x' => array(), 'len' => array());

		$val['x']['line'] = $len_prefix + 1;
		if($count === 1)
			$val['len']['line'] = 6;
		else
			$val['len']['line'] = 1 + ($count - 1)*4 + $count*2;

		$len_header = strlen($header_name) + (strlen($header_name)%2);
		if($len_header >= $val['len']['line'])
			$val['len']['line'] = $len_header + 2;

		$val['x']['box'] = $val['x']['line'] + $val['len']['line'];

		return $val;
	}

	// returns all column and length values needed for specified branch box
	private function map_val_branch($count, $n, $box_name)
	{
		if(!isset($count))
			throw new Exception('MISSING_PARAM:count');
		if(!isset($n))
			throw new Exception('MISSING_PARAM:n');
		if(!isset($box_name))
			throw new Exception('MISSING_PARAM:box_name');

		if(!isset($this->map_prefix_padding_length))
			throw new Exception("NOT_DEFINED:map_prefix_padding_length");
		$len_prefix = $this->map_prefix_padding_length + 1;

		$val = array('x' => array(), 'len' => array(), 'rows' => array());

		$val['x']['branch'] = $len_prefix + 3 + ($count - $n - 1)*4;
		$val['rows']['branch'] = -1 + $n*3;

		$val['x']['line'] = $val['x']['branch'] + $val['rows']['branch'];
		$val['len']['line'] = ($count - $n)*2;

		$val['x']['box'] = $val['x']['line'] + $val['len']['line'];

		return $val;
	}

	// setups the header of a map
	private function map_gen_header($type, $group, $box_name)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');
		if(!isset($box_name))
			throw new Exception('MISSING_PARAM:box_name');

		if(!isset($this->node[$type][$group]))
			throw new Exception("NOT_DEFINED:node:$type:$group");
		if(!is_array($this->node[$type][$group]))
			throw new Exception("NOT_AN_ARRAY:node:$type:$group");

		switch($type)
		{
			case 'master':
				$header_prefix = '  ';
				$header_name = 'MASTER';
				$line_prefix = '/';
				break;
			case 'peer':
				$header_prefix = '';
				$header_name = '';
				$line_prefix = '';
				break;
			case 'poller':
				$header_prefix = '| ';
				$header_name = "HOSTGROUP: $group";
				$line_prefix = '=';
				break;
			default:
				throw new Exception("INVALID_TYPE:$type");
				break;
		}

		try {

			// get number of nodes
			$count = count($this->node[$type][$group]);

			// get all needed offset and length values
			$val = $this->map_val_header($count, $header_name, $box_name);


			// generate box line 1
			$y = 0;
			$str = $this->map_get_prefix($header_prefix . $header_name);
			$this->map_insert_str($type, $group, 0, $y, $str);

			$box = $this->map_get_box($box_name);
			$this->map_insert_box($type, $group, $val['x']['box'], $y, $box);

			// generate box line 2
			$y++;
			$str = $this->map_get_prefix($line_prefix);
			$this->map_insert_str($type, $group, 0, $y, $str);


			$str = str_repeat('-', $val['len']['line']);
			$this->map_insert_str($type, $group, $val['x']['line'], $y, $str);


			// generate box line 3
			$y++;
			$str = $this->map_get_prefix('|');
			$this->map_insert_str($type, $group, 0, $y, $str);

		} catch(Exception $e) {
			throw new Exception('FAILED', 0, $e);
		}
	}

	// setups a branch box of a map
	private function map_gen_branch($type, $group, $n, $y, $box_name)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');
		if(!isset($n))
			throw new Exception('MISSING_PARAM:n');
		if(!isset($y))
			throw new Exception('MISSING_PARAM:y');
		if(!isset($box_name))
			throw new Exception('MISSING_PARAM:box_name');

		if(!isset($this->node[$type][$group]))
			throw new Exception("NOT_DEFINED:node:$type:$group");
		if(!is_array($this->node[$type][$group]))
			throw new Exception("NOT_AN_ARRAY:node:$type:$group");

		try {

			// get number of nodes
			$count = count($this->node[$type][$group]);

			// get all needed offset and length values
			$val = $this->map_val_branch($count, $n, $box_name);


			// generate box line 1
			$str = $this->map_get_prefix('|');
			$this->map_insert_str($type, $group, 0, $y, $str);

			$box = $this->map_get_box($box_name);
			$this->map_insert_box($type, $group, $val['x']['box'], $y, $box);

			// generate box line 2
			$y++;
			$str = $this->map_get_prefix('|');
			$this->map_insert_str($type, $group, 0, $y, $str);

			$str = str_repeat('-', $val['len']['line']);
			$this->map_insert_str($type, $group, $val['x']['line'], $y, $str);

			// generate box line 3
			$y++;
			$str = $this->map_get_prefix('|');
			$this->map_insert_str($type, $group, 0, $y, $str);

			// generate branch
			$this->map_insert_branch(
				$type,
				$group,
				$val['x']['branch'],
				$val['rows']['branch']
			);

		} catch(Exception $e) {
			throw new Exception('FAILED', 0, $e);
		}
	}

	// setups the footer of a map
	private function map_gen_footer($type, $group, $y)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');
		if(!isset($y))
			throw new Exception('MISSING_PARAM:y');

		try {

			$str = $this->map_get_prefix('|');
			$this->map_insert_str($type, $group, 0, $y, $str);
			$y++;
			$this->map_insert_str($type, $group, 0, $y, $str);

		} catch(Exception $e) {
			throw new Exception('FAILED', 0, $e);
		}
	}

	// inserts localhost into the peer map, regardless of map existing or not
	public function map_gen_local()
	{
		if(!isset($this->map['peer'][0][0]))
			$this->map['peer'][0][0] = array();
		if(!isset($this->map['peer'][0][1]))
			$this->map['peer'][0][1] = array();
		if(!isset($this->map['peer'][0][2]))
			$this->map['peer'][0][2] = array();
		// get_node_groups() will mess up unless there's a node array as well
		if(!isset($this->node['peer'][0]))
			$this->node['peer'][0] = array();

		try	{
			$padding = $this->map_prefix_padding_length;
			$x = $padding - 3;

			$box = $this->map_get_box('ipc');
			$this->map_insert_box('peer', 0, $x, 0, $box);

		} catch(Exception $e) {
			throw new Exception('FAILED', 0, $e);
		}
	}

	// removes prefix strings from a map where there's no need for it
	public function map_strip_last_prefix($type, $group)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');

		if(!isset($this->node[$type][$group]))
			throw new Exception("NOT_DEFINED:node:$type:$group");
		if(!is_array($this->node[$type][$group]))
			throw new Exception("NOT_AN_ARRAY:node:$type:$group");

		if(!isset($this->map[$type][$group]))
			throw new Exception("NOT_DEFINED:map:$type:$group");
		if(!is_array($this->map[$type][$group]))
			throw new Exception("NOT_AN_ARRAY:map:$type:$group");

		try	{

			$str = $this->map_get_prefix(' ');
			$y_max = count($this->map[$type][$group]);

			for($y = 2; $y < $y_max; $y++)
				$this->map_insert_str($type, $group, 0, $y, $str);

		} catch(Exception $e) {
			throw new Exception('FAILED', 0, $e);
		}

		return true;
	}

	// main function that generates map for specified type+group
	public function map_gen($type, $group)
	{
		if(!isset($type))
			throw new Exception('MISSING_PARAM:type');
		if(!isset($group))
			throw new Exception('MISSING_PARAM:group');


		if(!isset($this->map))
			throw new Exception('NOT_DEFINED:map');
		if(!is_array($this->map))
			throw new Exception('NOT_AN_ARRAY:map');

		if(!isset($this->node[$type][$group]))
			throw new Exception("NOT_DEFINED:node:$type:$group");
		if(!is_array($this->node[$type][$group]))
			throw new Exception("NOT_AN_ARRAY:node:$type:$group");

		try {

			$n = 0;
			$y = 0;
			foreach($this->node[$type][$group] as $node)
			{
				$box_name = $node['NAME'];

				if($n === 0) // header
					$this->map_gen_header($type, $group, $box_name);
				else // branches
					$this->map_gen_branch($type, $group, $n, $y, $box_name);

				$y += 3;
				$n++;
			}

			$this->map_gen_footer($type, $group, $y);

		} catch(Exception $e) {
			throw new Exception('FAILED', 0, $e);
		}
	}

}

if(!class_exists('NodeTree'))
        die('CLASS_NOT_FOUND:NodeTree');

$o = new NodeTree;

if(!is_object($o))
        die('MAIN_OBJECT_NOT_CREATED');

try {
	if(isset($argv[1]))
		if($argv[1] == '--help')
			$o->syntaxdie(0);
		else
			$o->syntaxdie(1);

	// parse node data
	$o->nodes_parse_show($o->execute('mon', 'node show'));

	// fetch current node types
	$types = $o->get_node_types();

	// generate main maps if any
	foreach($types as $type)
	{
		foreach($o->get_node_groups($type) as $group)
		{
			$o->nodes_sort($type, $group);

			$o->map_init($type, $group);

			$o->map_gen($type, $group);

			$o->map_strip($type, $group);
		}
	}

	// find bottom type+group if any with this pointless loop
	foreach($types as $type)
		foreach($o->get_node_groups($type) as $group)
			continue;

	// remove prefix at the very bottom if needed (not a single node system)
	if(isset($type) && isset($group))
		$o->map_strip_last_prefix($type, $group);

	// add local node box to peer map
	$o->map_gen_local();

	// map_gen_local() would have added peer type now if single node system
	$types = $o->get_node_types();

	// convert map arrays to text block
	$output = '';
	foreach($types as $type)
		foreach($o->get_node_groups($type) as $group)
			$output .= $o->map_output($type, $group);

	// write text block to stdout!
	echo $output;

} catch(Exception $e) {
	$o->exception_die($e);
}

exit(0);

?>
