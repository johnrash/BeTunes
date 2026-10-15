// Command line front end to the ID3 writer, for tests/tag-test.sh:
//   tagtool <file.mp3> [title=...] [artist=...] [album=...] [genre=...]
//       [year=...] [track=...] [disc=...]
#include <stdio.h>
#include <string.h>

#include "Id3Tag.h"


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <file.mp3> [field=value ...]\n", argv[0]);
		return 2;
	}
	TagChanges changes;
	for (int i = 2; i < argc; i++) {
		const char* equals = strchr(argv[i], '=');
		bool known = false;
		for (int field = 0; equals != NULL && field < TAG_FIELD_COUNT;
				field++) {
			if (strncmp(argv[i], kTagFieldNames[field], equals - argv[i]) == 0
				&& strlen(kTagFieldNames[field]) == (size_t)(equals - argv[i])) {
				changes.Set((tag_field)field, equals + 1);
				known = true;
			}
		}
		if (!known) {
			fprintf(stderr, "unknown field: %s\n", argv[i]);
			return 2;
		}
	}
	std::string error;
	int result = write_mp3_tags(argv[1], changes, NULL, NULL, error);
	if (result != 0) {
		fprintf(stderr, "%s: %s\n", argv[1], error.c_str());
		return 1;
	}
	return 0;
}
