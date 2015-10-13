#ifndef TOOLS_MERLINCAT_CONSOLE_H_
#define TOOLS_MERLINCAT_CONSOLE_H_

#include <shared/node.h>

struct ConsoleIO_;
typedef struct ConsoleIO_ ConsoleIO;

ConsoleIO *consoleio_new(void (*session_newline)(const char *, gpointer),
		gpointer user_data);

void consoleio_destroy(ConsoleIO *cio);

#endif /* TOOLS_MERLINCAT_CONSOLE_H_ */
