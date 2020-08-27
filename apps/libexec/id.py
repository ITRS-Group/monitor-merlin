import uuid

def cmd_generate(args):
	"""
	Prints a UUID4 to be used when Merlin is in UUID identification mode.
	"""

	print str(uuid.uuid4())
