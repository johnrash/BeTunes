// Prints a tag of an audio file as the Media Kit reads it (file and track
// metadata, key without regard to case), for tests/gui-test.sh:
//   readtags <file> <key>
#include <Entry.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <Message.h>

#include <stdio.h>
#include <strings.h>


static const char*
find(const BMessage& message, const char* key)
{
	char* name;
	type_code type;
	int32 count;
	for (int32 i = 0; message.GetInfo(B_STRING_TYPE, i, &name, &type, &count)
			== B_OK; i++) {
		const char* value;
		if (strcasecmp(name, key) == 0
			&& message.FindString(name, &value) == B_OK)
			return value;
	}
	return NULL;
}


int
main(int argc, char** argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s <file> <key>\n", argv[0]);
		return 2;
	}
	entry_ref ref;
	if (get_ref_for_path(argv[1], &ref) != B_OK)
		return 1;
	BMediaFile file(&ref);
	if (file.InitCheck() != B_OK)
		return 1;

	BMessage fileData;
	file.GetMetaData(&fileData);
	const char* value = find(fileData, argv[2]);
	for (int32 i = 0; value == NULL && i < file.CountTracks(); i++) {
		BMediaTrack* track = file.TrackAt(i);
		BMessage trackData;
		if (track != NULL && track->GetMetaData(&trackData) == B_OK)
			value = find(trackData, argv[2]);
		if (value != NULL)
			printf("%s\n", value);
		file.ReleaseTrack(track);
		if (value != NULL)
			return 0;
	}
	if (value != NULL)
		printf("%s\n", value);
	return value != NULL ? 0 : 1;
}
