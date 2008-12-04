
#include <glib/glist.h>

#include "getstream.h"

struct config_s	{
	GList			*adapter;
	int			http_port;
};

struct config_s *readconfig(char *filename);
