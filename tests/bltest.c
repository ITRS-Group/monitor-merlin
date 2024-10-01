#include "binlog.h"
#include "colors.h"
#include "test_utils.h"
#include "shared.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>


struct binlog {
	struct binlog_entry **cache;
	unsigned int write_index, read_index, file_entries;
	unsigned int alloc;
	unsigned int mem_size;
	unsigned long long int max_mem_size;
	unsigned int mem_avail;
	off_t max_file_size, file_size, file_read_pos, file_write_pos;
	int is_valid;
	int should_warn_if_full;
	char *path;
	char *file_metadata_path;
	char *file_save_path;
	int fd;
};

/* autogenerated message list, produced by fortune */
static char *msg_list[] = {
	"Life, loathe it or ignore it, you can't like it.",
	"SOMEONE ELSE.",
	"... all the modern inconveniences ...",
	"one when he was a boy and one when he was a man.",
	"Not Hercules could have knock'd out his brains, for he had none.",
	"When you ascend the hill of prosperity may you not meet a friend.",
	"You have been in Afghanistan, I perceive.",
	"Good afternoon, madam.  How may I help you?",
	"Good afternoon.  I'd like a FrintArms HandCannon, please.",
	"I'll take the special.",
	"Sound choice, madam, *sound* choice.  Now, do--?",
	"Aah... yes,  And how does madam wish to pay?",
	"Benson, you are so free of the ravages of intelligence",
	"First things first -- but not necessarily in that order",
	"It's kind of fun to do the impossible.",
	"My life is a soap opera, but who has the rights?",
	"No, `Eureka' is Greek for `This bath is too hot.'",
	"'Close to You'.  Hit it, boys!",
	"Rembrandt's first name was Beauregard, which is why he never used it.",
	"Spare no expense to save money on this one.",
	"and it will be a sell out.",
	"Truth is stranger than fiction, because fiction has to make sense.",
	"Well, that was a piece of cake, eh K-9?",
	"You've got to have a gimmick if your band sucks.",
	"Why bother? He's probably home reading the Encyclopedia Britannica.",
	"The Tao is embodied in all software -- regardless of how insignificant,",
	"I said, 'No.  Wrong.  Game over.  Next contestant, please.'",
	"But what we need to know is, do people want nasally-insertable computers?",
	"How do I love thee?  My accumulator overflows.",
	"If that makes any sense to you, you have a big problem.",
	"Is it PC compatible?",
	"It runs like _x, where _x is something unsavory",
	"It's not just a computer -- it's your ass.",
	"The Other Side.",
	"This ... this is your canvas! your clay!  Go forth and create a masterwork!",
	"Now this is a totally brain damaged algorithm.  Gag me with a smurfette.",
	"Nuclear war can ruin your whole compile.",
	"One basic notion underlying Usenet is that it is a cooperative.",
	"Pascal is Pascal is Pascal is dog meat.",
	"1 is prime, 1 is prime, 1 is prime, 1 is prime...",
	"The Computer made me do it.",
	"This is lemma 1.1.  We start a new chapter so the numbers all go back to one.",
	"We are on the verge: Today our program proved Fermat's next-to-last theorem.",
	"What is the Nature of God?",
	"I've just GOT to start labeling my software...",
	"You can't make a program without broken egos.",
	"If it ain't broke, don't fix it.",
	"The one charm of marriage is that it makes a life of deception a neccessity.",
	"God is a comedian playing to an audience too afraid to laugh.",
	"There are things that are so serious that you can only joke about them",
	"Confound these ancestors.... They've stolen our best ideas!",
	"All my life I wanted to be someone; I guess I should have been more specific.",
	"The greatest warriors are the ones who fight for peace.",
	"No matter where you go, there you are...",
	"I'm growing older, but not up.",
	"I hate the itching.  But I don't mind the swelling.",
	"Oh dear, I think you'll find reality's on the blink again.",
	"Send lawyers, guns and money...",
	"I go on working for the same reason a hen goes on laying eggs.",
	"Jesus may love you, but I think you're garbage wrapped in skin.",
	"Well the diagnostics say it's fine buddy, so it's a software problem.",
	"Show business is just like high school, except you get paid.",
	"This isn't brain surgery; it's just television.",
	"Morality is one thing.  Ratings are everything.",
	"Catch a wave and you're sitting on top of the world.",
	"That's a known problem... don't worry about it.",
	"I am your density.",
	"So why don't you make like a tree, and get outta here.",
	"Falling in love makes smoking pot all day look like the ultimate in restraint.",
	"I may kid around about drugs, but really, I take them seriously.",
	"Live or die, I'll make a million.",
	"Come on over here, baby, I want to do a thing with you.",
	"Ahead warp factor 1",
	"Of all the tyrannies that affect mankind, tyranny in religion is the worst.",
	"I say we take off; nuke the site from orbit.  It's the only way to be sure.",
	"Unibus timeout fatal trap program lost sorry",
	"If you'll excuse me a minute, I'm going to have a cup of coffee.",
	"I'm a mean green mother from outer space",
	"There is no statute of limitations on stupidity.",
	"We can't schedule an orgy, it might be construed as fighting",
	"Ada is the work of an architect, not a computer scientist.",
	"I think every good Christian ought to kick Falwell's ass.",
	"You need tender loving care once a week - so that I can slap you into shape.",
	"Why should we subsidize intellectual curiosity?",
	"Plan to throw one away.  You will anyway.",
	"Why should we subsidize intellectual curiosity?",
	"I have just one word for you, my boy...plastics.",
	"There is such a fine line between genius and stupidity.",
	"If Diet Coke did not exist it would have been neccessary to invent it.",
	"Your attitude determines your attitude.",
	"If you want to eat hippopatomus, you've got to pay the freight.",
	"We will bury you.",
	"Now here's something you're really going to like!",
	"How to make a million dollars:  First, get a million dollars.",
	"Language shapes the way we think, and determines what we can think about.",
	"For the love of phlegm...a stupid wall of death rays.  How tacky can ya get?",
	"Bureaucracy is the enemy of innovation.",
	"An organization dries up if you don't challenge it with growth.",
	"I've seen it.  It's rubbish.",
	"All Bibles are man-made.",
	"Spock, did you see the looks on their faces?",
	"Yes, Captain, a sort of vacant contentment.",
	"Gravitation cannot be held responsible for people falling in love.",
	"I think Michael is like litmus paper - he's always trying to learn.",
	"A verbal contract isn't worth the paper it's printed on.",
	"We shall reach greater and greater platitudes of achievement.",
	"With molasses you catch flies, with vinegar you catch nobody.",
	"Lead us in a few words of silent prayer.",
	"I couldn't remember things until I took that Sam Carnegie course.",
	"Ninety percent of baseball is half mental.",
	"jackpot:  you may have an unneccessary change record",
	"One lawyer can steal more than a hundred men with guns.",
	"One day I woke up and discovered that I was in love with tripe.",
	"When people are least sure, they are often most dogmatic.",
	"Nature is very un-American.  Nature never hurries.",
	"We learn from history that we learn nothing from history.",
	"Flattery is all right -- if you don't inhale.",
	"Consistency requires you to be as ignorant today as you were a year ago.",
	"Tell the truth and run.",
	"Never face facts; if you do, you'll never get up in the morning.",
	"Life is a garment we continuously alter, but which never seems to fit.",
	"It is easier to fight for principles than to live up to them.",
	"Success covers a multitude of blunders.",
	"Yes, and I feel bad about rendering their useless carci into dogfood...",
	"You're a creature of the night, Michael.  Wait'll Mom hears about this.",
	"Plastic gun.  Ingenious.  More coffee, please.",
	"Silent gratitude isn't very much use to anyone.",
	"But this one goes to eleven.",
	"Been through Hell?  Whaddya bring back for me?",
	"I've got some amyls.  We could either party later or, like, start his heart.",
	"Roman Polanski makes his own blood.  He's smart -- that's why his movies work.",
	"The following is not for the weak of heart or Fundamentalists.",
	"'Course you haven't, you fruit-loop little geek.",
	"Hi, I'm Professor Alan Ginsburg... But you can call me... Captain Toke.",
	"Time is money and money can't buy you love and I love your outfit",
	"Can't you just gesture hypnotically and make him disappear?",
	"You shouldn't make my toaster angry.",
	"Everyone is entitled to an *informed* opinion.",
	"May the forces of evil become confused on the way to your house.",
	"If it's not loud, it doesn't work!",
	"Hello again, Peabody here...",
	"It's the best thing since professional golfers on 'ludes.",
	"And remember: Evil will always prevail, because Good is dumb.",
	"We don't have to protect the environment -- the Second Coming is at hand.",
	"To YOU I'm an atheist; to God, I'm the Loyal Opposition.",
	"Only the hypocrite is really rotten to the core.",
	"The sixties were good to you, weren't they?",
	"You stay here, Audrey -- this is between me and the vegetable!",
	"You know, we've won awards for this crap.",
	"Mr. Spock succumbs to a powerful mating urge and nearly kills Captain Kirk.",
	"Poor man... he was like an employee to me.",
	"Trust me.  I know what I'm doing.",
	"If truth is beauty, how come no one has their hair done in the library?",
	"Look! There! Evil!.. pure and simple, total evil from the Eighth Dimension!",
	"I may be synthetic, but I'm not stupid",
	"Danger, you haven't seen the last of me!",
	"When anyone says `theoretically,' they really mean `not really.'",
	"No problem is so formidable that you can't walk away from it.",
	"For the man who has everything... Penicillin.",
	"The way of the world is to praise dead saints and prosecute live ones.",
	"It's a dog-eat-dog world out there, and I'm wearing Milkbone underware.",
	"Would I turn on the gas if my pal Mugsy were in there?",
	"Consequences, Schmonsequences, as long as I'm rich.",
	"And do you think (fop that I am) that I could be the Scarlet Pumpernickel?",
	"Kill the Wabbit, Kill the Wabbit, Kill the Wabbit!",
	"I DO want your money, because god wants your money!",
	"You show me an American who can keep his mouth shut and I'll eat him.",
	"There is hopeful symbolism in the fact that flags do not wave in a vacuum.",
	"Not only is God dead, but just try to find a plumber on weekends.",
	"Nuclear war would really set back cable.",
	"You tweachewous miscweant!",
	"Open Channel D...",
	"The pyramid is opening!",
	"The one with the ever-widening hole in it!",
	"My sense of purpose is gone! I have no idea who I AM!",
	"You are WRONG, you ol' brass-breasted fascist poop!",
	"Your mother was a hamster, and your father smelt of elderberries!",
	"Take that, you hostile sons-of-bitches!",
	"The voters have spoken, the bastards...",
	"Be there.  Aloha.",
	"When the going gets weird, the weird turn pro...",
	"Say yur prayers, yuh flea-pickin' varmint!",
	"There... I've run rings 'round you logically",
	"Let's show this prehistoric bitch how we do things downtown!",
	"Just the facts, Ma'am",
	"I have five dollars for each of you.",
	"In the fight between you and the world, back the world.",
	"Don't worry about the mule.  Just load the wagon.",
	"Being against torture ought to be sort of a bipartisan thing.",
	"Here comes Mr. Bill's dog.",
	"I will make no bargains with terrorist hardware.",
	"Dump the condiments.  If we are to be eaten, we don't need to taste good.",
	"Aww, if you make me cry anymore, you'll fog up my helmet.",
	"I got everybody to pay up front...then I blew up their planet.",
	"Atomic batteries to power, turbines to speed.",
	"I just want to be a good engineer.",
	"When in doubt, print 'em out.",
	"We came.  We saw.  We kicked its ass.",
	"Laugh while you can, monkey-boy.",
	"Floggings will continue until morale improves.",
	"Hey Ivan, check your six.",
	"Free markets select for winning solutions.",
	"The urge to destroy is also a creative urge.",
	"Wish not to seem, but to be, the best.",
	"Survey says...",
	"Paul Lynde to block...",
	"Little else matters than to write good code.",
	"Stupidity, like virtue, is its own reward",
	"If a computer can't directly address all the RAM you can use, it's just a toy.",
	"A dirty mind is a joy forever.",
	"You can't teach seven foot.",
	"A car is just a big purse on wheels.",
	"History is a tool used by politicians to justify their intentions.",
	"Nine years of ballet, asshole.",
	"Being against torture ought to be sort of a multipartisan thing.",
	"Facts are stupid things.",
	"An ounce of prevention is worth a ton of code.",
	"Just think of a computer as hardware you can program.",
	"Everything should be made as simple as possible, but not simpler.",
	"Card readers?  We don't need no stinking card readers.",
	"Gotcha, you snot-necked weenies!",
	"Everybody is talking about the weather but nobody does anything about it.",
	"How many teamsters does it take to screw in a light bulb?",
	"If you weren't my teacher, I'd think you just deleted all my files.",
	"The medium is the message.",
	"The medium is the massage.",
	"Show me a good loser, and I'll show you a loser.",
	"It might help if we ran the MBA's out of Washington.",
	"Love may fail, but courtesy will previal.",
	"There is no distinctly American criminal class except Congress.",
	"You'll pay to know what you really think.",
	"We live, in a very kooky time.",
	"Pull the wool over your own eyes!",
	"Our reruns are better than theirs.",
	"Pay no attention to the man behind the curtain.",
	"Pay no attention to the man behind the curtain.",
	"Don't discount flying pigs before you have good air defense.",
	"In the long run, every program becomes rococo, and then rubble.",
	"Pok pok pok, P'kok!",
	"You can't get very far in this world without your dossier being there first.",
	"Your butt is mine.",
	"Once they go up, who cares where they come down?  That's not my department.",
	"Imitation is the sincerest form of television.",
	"The lesser of two evils -- is evil.",
	"Is this foreplay?",
	"A mind is a terrible thing to have leaking out your ears.",
	"Life sucks, but it's better than the alternative.",
	"Even if you're on the right track, you'll get run over if you just sit there.",
	"An open mind has but one disadvantage: it collects dirt.",
	"The geeks shall inherit the earth.",
	"Beware of programmers carrying screwdrivers.",
	"Elvis is my copilot.",
	"Let us condemn to hellfire all those who disagree with us.",
	"The number of Unix installations has grown to 10, with more expected.",
	"Engineering without management is art.",
	"I'm not a god, I was misquoted.",
	"Psychoanalysis??  I thought this was a nude rap session!!!",
	"Seed me, Seymour",
	"Buy land.  They've stopped making it.",
	"Open the pod bay doors, HAL.",
	"This generation may be the one that will face Armageddon.",
	"The only way for a reporter to look at a politician is down.",
	"If the code and the comments disagree, then both are probably wrong.",
	"May your future be limited only by your dreams.",
	"Freedom is still the most radical idea of all.",
	"I never let my schooling get in the way of my education.",
	"Thank heaven for startups; without them we'd never have any advances.",
	"Out of register space (ugh)",
	"'Tis true, 'tis pity, and pity 'tis 'tis true.",
	"Indecision is the basis of flexibility",
	"Sometimes insanity is the only alternative",
	"Old age and treachery will beat youth and skill every time.",
	"The most important thing in a man is not what he knows, but what he is.",
	"All we are given is possibilities -- to make ourselves one thing or another.",
	"From there to here, from here to there, funny things are everywhere.",
	"When it comes to humility, I'm the greatest.",
	"Engineering meets art in the parking lot and things explode.",
	"Never give in.  Never give in.  Never. Never. Never.",
	"Never ascribe to malice that which is caused by greed and ignorance.",
	"I prefer rogues to imbeciles, because they sometimes take a rest.",
	"Everyone's head is a cheap movie show.",
	"Were there no women, men might live like gods.",
	"Intelligence without character is a dangerous thing.",
	"It's not just a computer -- it's your ass.",
	"Let me guess, Ed.  Pentescostal, right?",
	"BTW, does Jesus know you flame?",
	"I've seen the forgeries I've sent out.",
	"Bite off, dirtball.",
	"You who hate the Jews so, why did you adopt their religion?",
	"Faith:  not *wanting* to know what is true.",
	"If you are afraid of loneliness, don't marry.",
	"In matrimony, to hesitate is sometimes to be saved.",
	"It will tell us.",
	"Inquiry is fatal to certainty.",
	"Cogito ergo I'm right and you're wrong.",
	"Your stupidity, Allen, is simply not up to par.",
	"Yours is.",
	"Jesus saves...but Gretzky gets the rebound!",
	"Anything created must necessarily be inferior to the essence of the creator.",
	"Einstein's mother must have been one heck of a physicist.",
	"Any excuse will serve a tyrant.",
	"Experience has proved that some people indeed know everything.",
	"I prefer the blunted cudgels of the followers of the Serpent God.",
	"my terminal is a lethal teaspoon.",
	"I am ... a woman ... and ... technically a parasitic uterine growth",
	"Money is the root of all money.",
	"...Greg Nowak:  `Another flame from greg' - need I say more?",
	"No.  You need to say less.",
	"Marriage is low down, but you spend the rest of your life paying for it.",
	"The chain which can be yanked is not the eternal chain.",
	"Go to Heaven for the climate, Hell for the company.",
	"Well hello there Charlie Brown, you blockhead.",
	"Time is an illusion.  Lunchtime doubly so.",
	"Ignorance is the soil in which belief in miracles grows.",
	"Let every man teach his son, teach his daughter, that labor is honorable.",
	"I have not the slightest confidence in 'spiritual manifestations.'",
	"It is hard to overstate the debt that we owe to men and women of genius.",
	"Joy is wealth and love is the legal tender of the soul.",
	"The hands that help are better far than the lips that pray.",
	"Be *excellent* to each other.",
	"I'm not afraid of dying, I just don't want to be there when it happens.",
	"The Street finds its own uses for technology.",
	"What the scientists have in their briefcases is terrifying.",
	"...a most excellent barbarian ... Genghis Kahn!",
	"Pull the trigger and you're garbage.",
	"Oh what wouldn't I give to be spat at in the face...",
	"If you can't debate me, then there is no way in hell you'll out-insult me.",
	"You may be wrong here, little one.",
	"I am, therefore I am.",
	"It's what you learn after you know it all that counts.",
	"We want to create puppets that pull their own strings.",
	"Would this make them Marionettes?",
	"Love your country but never trust its government.",
	"Turn on, tune up, rock out.",
	"Life sucks, but death doesn't put out at all....",
	"Life, loathe it or ignore it, you can't like it.",
	"A mere abacus.  Mention it not.",
	"America is a stronger nation for the ACLU's uncompromising effort.",
	"Luke, I'm yer father, eh.  Come over to the dark side, you hoser.",
	"Don't think; let the machine do it for you!",
	"It ain't over until it's over.",
	"If anything can go wrong, it will.",
	"Yo baby yo baby yo.",
	"Nuclear war can ruin your whole compile.",
	"Remember, extremism in the nondefense of moderation is not a virtue.",
	"What man has done, man can aspire to do.",
	"If you can, help others.  If you can't, at least don't hurt others.",
	"Just think, with VLSI we can have 100 ENIACS on a chip!",
	"Gort, klaatu nikto barada.",
	"Dale, your address no longer functions.  Can you fix it at your end?",
	"Bill, Your brain no longer functions.  Can you fix it at your end?",
	"Don't drop acid, take it pass-fail!",
	"I got a question for ya.  Ya got a minute?",
	"Help Mr. Wizard!",
	"Don't talk to me about disclaimers!  I invented disclaimers!",
	"Cable is not a luxury, since many areas have poor TV reception.",
	"It's when they say 2 + 2 = 5 that I begin to argue.",
	"You can have my Unix system when you pry it from my cold, dead fingers.",
	"Vogons may read you bad poetry, but bogons make you study obsolete RFCs.",
	"That's just like the episode where Jan loses her glasses!",
	"Hey, aren't you the string I just threw out of here?",
	"Hey!  Who took the cork off my lunch?!",
	"Mind if I smoke?",
	"Mind if I smoke?",
	"The whole world is about three drinks behind.",
	"A University without students is like an ointment without a fly.",
	"He was a modest, good-humored boy.  It was Oxford that made him insufferable.",
	"I am not sure what this is, but an `F' would only dignify it.",
	"I have to convince you, or at least snow you ...",
	"If you understand what you're doing, you're not learning anything.",
	"OK, now let's look at four dimensions on the blackboard.",
	"Plaese porrf raed.",
	"Speed is subsittute fo accurancy.",
	"We demand rigidly defined areas of doubt and uncertainty!",
	"All snakes who wish to remain in Ireland will please raise their right hands.",
	"Gee, Toto, I don't think we are in Kansas anymore.",
	"God gives burdens; also shoulders",
	"I'm in Pittsburgh.  Why am I here?",
	"You must be from New York.",
	"Old MacDonald had a . . .",
	"Eat, drink, and be merry, for tomorrow you may work.",
	"Life is like a buffet; it's not good but there's plenty of it.",
	"what's the first thing you say to yourself?",
	"A power so great, it can only be used for Good or Evil!",
	"Boy, life takes a long time to live.",
	"But I don't like Spam!!!!",
	"Humor is a drug which it's the fashion to abuse.",
	"If I melt dry ice, can I take a bath without getting wet?",
	"Right now I'm having amnesia and deja vu at the same time.",
	"In the abstract, yes, but not in the concrete.",
	"So will you.",
	"Elwood, in this world you must be oh so smart or oh so pleasant.",
	"The only real way to look younger is not to be born so soon.",
	"There was a boy called Eustace Clarence Scrubb, and he almost deserved it.",
	"We've taught our boy everything we know, he's fit to be tide.",
	"I was afraid you would waver under testimony.",
	"All language designers are arrogant.  Goes with the territory...",
	"Besides, I think [Slackware] sounds better than 'Microsoft,' don't you?",
	"I'm an idiot.. At least this one [bug] took about 5 minutes to find..",
	"It's God.  No, not Richard Stallman, or Linus Torvalds, but God.",
	"...[Linux's] capacity to talk via any medium except smoke signals.",
	"Never make any mistaeks.",
	"On the Internet, no one knows you're using Windows NT",
	"sic transit discus mundi",
	"We all know Linux is great...it does infinite loops in 5 seconds.",
	"Whip me.  Beat me.  Make me maintain AIX.",
	"Who is General Failure and why is he reading my hard disk ?",
	"World domination.  Fast",
	"I'd love to go out with you, but I have to floss my cat.",
	"I'd love to go out with you, but I have to stay home and see if I snore.",
	"I'd love to go out with you, but I never go out on days that end in `Y.'",
	"I'd love to go out with you, but I want to spend more time with my blender.",
	"I'd love to go out with you, but I'm attending the opening of my garage door.",
	"I'd love to go out with you, but I'm having all my plants neutered.",
	"I'd love to go out with you, but I'm taking punk totem pole carving.",
	"I'd love to go out with you, but I've been scheduled for a karma transplant.",
	"I'd love to go out with you, but it's my parakeet's bowling night.",
	"I'd love to go out with you, but my favorite commercial is on TV.",
	"I'd love to go out with you, but the last time I went out, I never came back.",
	"I'd love to go out with you, but the man on television told me to stay tuned.",
	"OK, you win, I give up.  Where did you hide the ship?",
	"Sure, I put your dog in the microwave.  But I feel *better* for doing it.",
	"... the Mayo Clinic, named after its founder, Dr. Ted Clinic ...",
	"What happened?",
	"It's men like him that give the Y chromosome a bad name.",
	"This is a parson to parson call.",
	"Whatever the missing mass of the universe is, I hope it's not cockroaches!",
	"Earth is a great, big funhouse without the fun.",
	"He flung himself on his horse and rode madly off in all directions.",
	"I don't mind going nowhere as long as it's an interesting path.",
	"I found out why my car was humming.  It had forgotten the words.",
	"I only touch base with reality on an as-needed basis!",
	"If a camel flies, no one laughs if it doesn't get very far.",
	"It was a virgin forest, a place where the Hand of Man had never set foot.",
	"To vacillate or not to vacillate, that is the question ... or is it?",
	"What's the use of a good quotation if you can't change it?",
	"No self-respecting fish would want to be wrapped in that kind of paper.",
	"All my life I wanted to be someone; I guess I should have been more specific.",
	"Apathy is not the problem, it's the solution",
	"Arguments with furniture are rarely productive.",
	"He's the kind of man for the times that need the kind of man he is ...",
	"I didn't know it was impossible when I did it.",
	"I've seen, I SAY, I've seen better heads on a mug of beer",
	"If we were meant to fly, we wouldn't keep losing our luggage.",
	"Maybe we can get together and show off to each other sometimes.",
	"No one gets too old to learn a new way of being stupid.",
	"See - the thing is - I'm an absolutist.  I mean, kind of ... in a way ...",
	"That boy's about as sharp as a pound of wet liver",
	"The Schizophrenic: An Unauthorized Autobiography",
	"They told me I was gullible ... and I believed them!",
	"They're unfriendly, which is fortunate, really.  They'd be difficult to like.",
	"I don't know.",
	"You can't teach people to be lazy - either they have it, or they don't.",
	"I'm here t'git the man that shot muh paw.",
	"A radioactive cat has eighteen half-lives.",
	"An ounce of prevention is worth a pound of purge.",
	"I think that I think, therefore I think that I am.",
	"Cogito ergo I'm right and you're wrong.",
	"Life is too important to take seriously.",
	"The porcupine with the sharpest quills gets stuck on a tree more often.",
	"MOKE DAT YIGARETTE",
	"Cable is not a luxury, since many areas have poor TV reception.",
	"... gentlemen do not read each other's mail.",
	"Give me enough medals, and I'll win any war.",
	"I don't care who does the electing as long as I get to do the nominating.",
	"I'm not stupid, I'm not expendable, and I'M NOT GOING!",
	"I'm willing to sacrifice anything for this cause, even other people's lives.",
	"If the King's English was good enough for Jesus, it's good enough for me!",
	"Never underestimate the power of a small tactical nuclear weapon.",
	"Nuclear war would really set back cable.",
	"legitimate desire of the prisoner to regain his liberty.",
	"They make a desert and call it peace.",
	"Those who do not do politics will be done in by politics.",
	"Light, bearing on the starboard bow.",
	"Ubi non accusator, ibi non judex.",
	"355/113 -- Not the famous irrational number PI, but an incredible simulation!",
	"Anything created must necessarily be inferior to the essence of the creator.",
	"Einstein's mother must have been one heck of a physicist.",
	"Consider a spherical bear, in simple harmonic motion...",
	"One, two, five.",
	"And he didn't understand me.",
	"Gentlemen, what we have here is a terrifying example of the reindeer effect.",
	"If value corrupts then absolute value corrupts absolutely.",
	"In short, _N is Richardian if, and only if, _N is not Richardian.",
	"Irrationality is the square root of all evil",
	"It's easier said than done.",
	"Obviously, a major malfunction has occurred.",
	"This isn't right.  This isn't even wrong.",
	"Our vision is to speed up time, eventually eliminating it.",
	"The four building blocks of the universe are fire, water, gravel and vinyl.",
	"The identical is equal to itself, since it is different.",
	"We don't care.  We don't have to.  We're the Phone Company.",
	"What I've done, of course, is total garbage.",
	"When the going gets tough, the tough get empirical.",
	"Yeah, but you're taking the universe out of context.",
	"To hear my character lied about!",
	"How good, how good does it feel to be free?",
	"Are birds free from the chains of the sky-way?",
	"I tot I taw a puddy tat.",
	"That eye is like this eye",
	"That definition's just.",
	"Force is not might but must!",
	"The nights are rather damp.",
	"You can never--",
	"I thought that you said you were 20 years old!",
	"And you claimed to be very near two meters tall!",
	"You said you were blonde, but you lied!",
	"Better head back to Tennessee Jed",
	"Dreamer!  Get your head out of the clouds.",
	"The Four Corners of the Round Table.",
	"Nice poem Tom.  I have ideas for changes though, why not come over?",
	"Lines that are parallel meet at Infinity!",
	"My name is Sue!  How do you do?!  Now you gonna die!",
	"No program is perfect,",
	"Pipe a song about a Lamb!",
	"Piper, pipe that song again;",
	"I have answered three questions and that is enough,",
	"I have answered three questions, and that is enough,",
	"Today I will be brilliant.",
	"I'm a doctor, not a mechanic.",
	"I'm a doctor, not an escalator.",
	"I'm a doctor, not a bricklayer.",
	"I'm a doctor, not an engineer.",
	"I'm a doctor, not a coalminer.",
	"I'm a surgeon, not a psychiatrist.",
	"I'm no magician, Spock, just an old country doctor.",
	"What am I, a doctor or a moonshuttle conductor?",
	"Just lie down on the floor and keep calm.",
	"Thou art That...",
	"The chain which can be yanked is not the eternal chain.",
	"You can't survive by sucking the juice from a wet mitten.",
	"Der bestirnte Himmel �ber mir und das moralische Gesetz in mir",
	"The starry sky above me, and the Moral Law inside me.",
	"At least they're ___________EXPERIENCED incompetents",
	"But don't you worry, its for a cause -- feeding global corporations' paws.",
	"but the important thing is that they're not about to admit it.",
	"Consequences, Schmonsequences, as long as I'm rich.",
	"Every man has his price.  Mine is $3.95.",
	"No job too big; no fee too big!",
	"Lookit all them WIRES in there!",
	"That's not a Porsche, it's a Ferrari.",
	"I recommend this candidate with no qualifications whatsoever.",
	"The amount of mathematics she knows will surprise you.",
	"I simply can't say enough good things about him.",
	"I am pleased to say that this candidate is a former colleague of mine.",
	"You won't find many people like her.",
	"I cannot reccommend him too highly.",
	"Her input was always critical.",
	"I have no doubt about his capability to do good work.",
	"She is quite uniform in her approach to any function you may assign her.",
	"You will be fortunate if you can get him to work for you.",
	"Success will never spoil him.",
	"One usually comes away from him with a good feeling.",
	"He should go far.",
	"He will take full advantage of his staff.",
	"Who is General Failure and why is he reading my hard disk?",
	"And I don't like doing silly things (except on purpose).",
	"Let's call it an accidental feature. :-)",
	"It is easier to port a shell than a shell script.",
	"The road to hell is paved with melting snowballs.",
	"Now we'll have to kill you.",
	"I am ecstatic that some moron re-invented a 1995 windows fuckup.",
	"HAHAHAHAHA!!  That's good, I like it..",
	"Um, thanks, they make us say that.",
	"I have a bone to pick, and a few to break.",
	"So, will the Andover party have a cash bar?",
	"No, there's free beer.",
	"Uh-oh, Stallman's gonna be pissed...",
	"What are we going to do tonight, Bill?",
	"Same thing we do every night Steve, try to take over the world!",
	"This is a job for BOB VIOLENCE and SCUM, the INCREDIBLY STUPID MUTANT DOG.",
};

struct test_binlog {
	char *path;
	char *name;
	unsigned long long int msize, fsize;
};
static struct test_binlog test[] = {
	{ "/tmp/binlog-test", "All in memory", 10000000, 1000000 },
	{ "/tmp/binlog-test", "All on disk", 0, 1000000 },
	{ "/tmp/binlog-test", "Some on disk", 4096, 1000000 },
	{ "/tmp/binlog-test", "not enough space", 3, 3 },
};

static void print_test_params(struct test_binlog *t)
{
	if (!t)
		return;
	printf("  path: %s\n  name: %s\n  msize: %u\n  fsize: %u\n\n",
		   t->path, t->name, t->msize, t->fsize);
}

static void fail(const char *msg, struct test_binlog *t)
{
	t_fail("%s", msg);
	print_test_params(t);
}

static int test_binlog(struct test_binlog *t, binlog *bl)
{
	size_t ok = 0;
	uint i;

	for (i = 0; i < ARRAY_SIZE(msg_list); i++) {
		int result;
		uint msg_len;

		msg_len = strlen(msg_list[i]) + 1;

		result = binlog_add(bl, msg_list[i], msg_len);
		if (msg_len > t->msize && msg_len > t->fsize && result == BINLOG_ENOSPC) {
			ok++;
			continue;
		}
	}

	if (!binlog_has_entries(bl))
		return ok - ARRAY_SIZE(msg_list);

	ok = 0;
	for (i = 0; i < ARRAY_SIZE(msg_list); i++) {
		char *str = NULL;
		int result;
		uint len, msg_len;

		msg_len = strlen(msg_list[i]) + 1;
		result = binlog_read(bl, (void **)&str, &len);
		if (result == BINLOG_EMPTY) {
			printf("binlog claims it's empty when just added to\n");
			continue;
		}

		if (len != msg_len) {
			printf("bad length returned. Expected %u, got %u\n", msg_len, len);
		}

		if (!str) {
			printf("got '%p' back\n", str);
		} else if (strcmp(str, msg_list[i])) {
			printf("strings returned don't match.\n");
		}
		else
			ok++;

		free(str);
	}

	for (i = 0; i < ARRAY_SIZE(msg_list); i++) {
		binlog_add(bl, msg_list[i], strlen(msg_list[i]) + 1);
	}

	binlog_wipe(bl, BINLOG_UNLINK);
	if (binlog_has_entries(bl))
		fail("Wiped binlog claims to have entries", NULL);
	else
		t_pass("Wiped binlog has no entries");
	binlog_invalidate(bl);
	if (binlog_is_valid(bl))
		fail("invalidated binlog claims to be valid", NULL);
	else
		t_pass("invalidated binlog is invalid");

	if (ok == ARRAY_SIZE(msg_list))
		return 0;

	return 1;
}

/*
 * Test the binlog api for leaks. This requires valgrind
 */
static void test_binlog_leakage(void)
{
	int i = 0, expect_end = 0;
	struct binlog *bl;
	uint len;
	char *p;
#define LARGE_MSIZE (5 << 20)
#define LARGE_FSIZE (10 << 20)
#define LARGE_PATH "/tmp/large-binlog"
	bl = binlog_create(LARGE_PATH, LARGE_MSIZE, LARGE_FSIZE, BINLOG_UNLINK);
	while (binlog_size(bl) < LARGE_MSIZE) {
		i = (i + 1) % ARRAY_SIZE(msg_list);
		if (binlog_add(bl, msg_list[i], strlen(msg_list[i])) < 0) {
			printf("binlog_add() failed\n");
			break;
		}
	}
	binlog_add(bl, "LAST", sizeof("LAST"));
	while (!binlog_read(bl, (void **)&p, &len)) {
		if (expect_end || (len == sizeof("LAST") && !strcmp(p, "LAST")))
			expect_end++;
		free(p);
	}
	if (expect_end == 1)
		t_pass("Transitioning from memory to file");
	else
		t_fail("Transitioning from memory to file");

	binlog_destroy(bl, BINLOG_UNLINK);
}

/*
 * Test the binlog api for empty binlog file
 */
static void test_binlog_empty(void)
{
#define FILE_BLOG "/tmp/empty-binlog"
#define FILE_META "/tmp/empty-binlog.meta"
#define FILE_SAVE "/tmp/empty-binlog.save"
	struct binlog *bl, *saved_binlog;
	FILE *meta, *save;

	// log_grok_var("log_file", "stdout");
	// log_grok_var("log_level", "debug");
	// log_grok_var("use_syslog", "1");
	// log_init();

	/* Create empty files */
	meta = fopen(FILE_META, "w");
	if (!meta) {
		t_fail("Failed to open %s", FILE_META);
		return;
	}
	save = fopen(FILE_SAVE, "w");
	if (!save) {
		t_fail("Failed to open %s", FILE_SAVE);
		fclose(meta);
		return;
	}
	fclose(meta);
	fclose(save);

	bl = binlog_create(FILE_BLOG, binlog_max_memory_size * 1024 * 1024, binlog_max_file_size * 1024 * 1024, BINLOG_UNLINK);
	if (!bl) {
		t_fail("Failed to create binlog");
		return;
	}

	saved_binlog = binlog_get_saved(bl);
	if (saved_binlog) {
		t_fail("Unexpected saved binlog returned");
		binlog_destroy(bl, BINLOG_UNLINK);
		return;
	} else {
		t_pass("No saved binlog returned");
	}

	if( access( bl->file_metadata_path, F_OK ) == 0 ) {
		t_fail("Metadata file should not exist");
		return;
	}

	if( access( bl->file_save_path, F_OK ) == 0 ) {
		t_fail("Save file should not exist");
		return;
	}

	t_pass("Metadata handling is correct");
}

int main(__attribute__((unused)) int argc, __attribute__((unused)) char **argv)
{
	uint i;

	t_set_colors(0);
	t_start("binlog tests");
	if (isatty(fileno(stdout))) {
		green = CLR_GREEN;
		red = CLR_RED;
		reset = CLR_RESET;
	} else {
		green = red = reset = "";
	}

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		struct test_binlog *t = &test[i];
		struct binlog *bl;
		struct stat st;

		bl = binlog_create(t->path, t->msize, t->fsize, BINLOG_UNLINK);
		if (!bl) {
			fail("Failed to create binlog", t);
			continue;
		}

		if (!test_binlog(t, bl)) {
			t_pass("%s", t->name);
		} else {
			fail(t->name, t);
		}
		binlog_destroy(bl, 1);

		if (stat(t->path, &st) < 0) {
			t_pass("binlog_destroy(bl, 1) removes the fully read file");
		} else {
			fail("binlog_destroy(bl, 1) fails to remove the fully read file", t);
		}
		unlink(t->path);
	}

	test_binlog_leakage();
	test_binlog_empty();
	return t_end();
}
