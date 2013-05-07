import os, sys

modpath = os.path.dirname(os.path.abspath(__file__)) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from merlin_apps_utils import *

def cmd_merlin(discard):
	"""
	Draws the Merlin logo in ascii art.
	"""
	print(color)
	print("""%s%s
  ... . ...   . .   .  .   ..        .=M7        .   ..   .
...  . .      . . .     .           ?MMM. .     .    .
. .       . .. . .   .  ... . .   =MMMM=...     ... ...  .
    . . . ..       .          . ,MMMMM? .   .    .   .   .
  . .. .       .  .            $MMMMMD      ..   .   .   ..
 . . . . .      .    ..      .MMMMMMM      .   ..      ...
          .   .   .   .     .MMMMMMMI   .     .  ..  .. ...
  .     .                   MMMMMMMM     ..   . .     .
  ..                   .. .MMMMMMMMM. . . . .         .   .
                 .        DNNNNNNMMM
       . . .       .    .DNNNNNNNNNN .           .   .   .
                 .   . .MNNNNNNNNNNN.            .
      . . .         MMMMMMMMMI ?NNNN
    .                .MNNNNNNMDN.7NN
     .  ..      .   .MNNONNNNNNNN.N,    ..           ..
.  .     .         ,M+   . .NNNNN          .. .         .. .
    ..... ..    MNDDD . .   .ZDDNDNM.   ..  .         .
 . .       .,MNDDDDDD     .  ZDDDDDDDDD.. .   . . . ... . .
 .        DNDDDDDDDDO        NDD. $DDDDDM,       .    .   .
   .    NDDDDDDDDO  I~       DDD~OMIDDDDDDDI     .  .. . .
 DM     888888DD.  .MD   .   ODDDDD. +DDDDDDD,       .   . .
. OM8MMMD888888     D8  .   .7DDD?     DDDDDDDDMD
     $MM888888~.    88$8     =88       .DDDDDDDDDD
 7$$MMMMMMN8888  . .8888O.   :88..   ..  8888DDDDDDD      .
 .$I . M.MM 888..   88888$   =888        M888888MMMM
     ..MMM.    .    888888: .8888Z  .   .888,NMOMMMM     .
      DM:+    .   . 8888888 Z88888$      I    =8ZMZ   ..  .
..  .,M....    .    ?OOOOOO,8888888D    . ..  .MMM   . ..
         .           OOOOOOOOOOOOOOO88:  .    .MMMM,  .
    .       .   .    ?OOOOOOOOOOOOOOOOO~   .   . 8..   . . .
 .   . . .    .  ..  .OOOOOOOOOOOOOOOOOZ.   . .  .O~ .   .
.    ..           .. .ZOOOOOOOOOOOOOOZ ,8.      ...,8 . ..
. .    .   .  .  .   .=OOOOOO: .$OOO,.  ,OD.. .   . .O, .. .
    .      .  . ZMMMMMN8$ZOOZ . . . . . .$OO7.        +D  .
  .       ..  $OOOOOOOOOOOOOI       .      ,OON$,...,.  Z?
     .       .   . ..:~+=.   .   .   .   .   ZOOOOOOOOON
               ..       .   .   .   .   .   .  ~7ZZZZZZO+
   .              .   .    . . .   .    .            .      %s""" %
(color.blue, color.bright, color.reset))

def cmd_ninja(discard):
	"""
	Draws the Ninja logo in ascii art.
	"""
	print("""%s%s%s%s                                                                   
             ..     ...::::::::::::::::::::...     ..                
            .8$, .:MMMMMMMMMMMMMMMMMMMMMMMMMMMM:. .$8=.              
           MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM             
          ,MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM.            
          MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM            
         .MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM.           
         ~MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM~           
         ZMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM.          
        .NMMMMMMD~                                I8MMMMMM,          
        .MMMMM$,                                    .MMMMM,          
        .MMMM:  DMMMMM7                     ?DMMMMM,  .MMM,          
        .MMMM    MMMMMMM7                  MMMMMMM.    MMM,          
        .MMMMM     :DMMMM.                IMMMN~     =MMMM,          
        .MMMMMMM8:                                ?OMMMMMM,          
        .MMMMMMMMMMM8O?+=~~~~~~~~~~~~~~~~~~=+?ZNMMMMMMMMMM,          
        .MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,          
       ~MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM+.        
     =MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMI       
    NMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,     
    MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM     
    MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM8     
    .MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM.      
     .?MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM.       
        .MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,          
        .MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,          
        .MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,          
        .MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,          
        .MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,          
        .MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,          
        .MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,          
        .MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,          
        .DMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM.          
         ?MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM           
         :MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM           
          MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM8           
          .MMMMMMM.                               .MMMMMMN.          
           .MMMMM.                                 .MMMMM.           
            :MMM=                                   =MMM:            
             CM#                                     #MD             %s""" %
(color.bright + color.esc, "40;7m", ' ', ' ', color.reset))

def cmd_screensaver(args):
	"""
	The most awesomest console-based screensaver in the entire world.
	"""
	interval = 5
	for arg in args:
		interval = arg

	interval = float(interval)

	if not interval:
		interval = 5

	try:
		while True:
			cmd_ninja(args)
			time.sleep(interval)
			cmd_merlin(args)
			time.sleep(interval)
	except:
		sys.exit(0)
