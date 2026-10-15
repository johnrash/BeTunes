// Prints the RMS level (dB full scale) of the raw float samples that the
// silent output writes with BETUNES_AUDIO_DUMP, from a byte offset to the
// end. Exact zeros (silence before the audio arrives) are left out.
//   levels <file> [offset]
#include <math.h>
#include <stdio.h>
#include <stdlib.h>


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <file> [offset]\n", argv[0]);
		return 2;
	}
	FILE* file = fopen(argv[1], "rb");
	if (file == NULL) {
		perror(argv[1]);
		return 2;
	}
	long offset = argc > 2 ? atol(argv[2]) : 0;
	fseek(file, offset, SEEK_SET);

	float buffer[4096];
	double sum = 0;
	long count = 0;
	size_t read;
	while ((read = fread(buffer, sizeof(float), 4096, file)) > 0) {
		for (size_t i = 0; i < read; i++) {
			if (buffer[i] != 0) {
				sum += (double)buffer[i] * buffer[i];
				count++;
			}
		}
	}
	fclose(file);

	if (count == 0) {
		printf("-200\n");
		return 1;
	}
	printf("%.2f\n", 10 * log10(sum / count));
	return 0;
}
