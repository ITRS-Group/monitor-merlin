/*
 * This file is a silly wrapper which enables us to write tests
 * for daemon-specific functionality without having the
 * 'main' functions clash horribly.
 */
extern int merlind_main(int argc, char **argv);

int main(int argc, char **argv)
{
	return merlind_main(argc, argv);
}
