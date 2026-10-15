// Renders an HVIF file at several sizes into one PNG (for checking the icon):
//   render_icon icon.hvif out.png
#include <stdio.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <File.h>
#include <IconUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <View.h>

int
main(int argc, char** argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s icon.hvif out.png\n", argv[0]);
		return 1;
	}
	BFile file(argv[1], B_READ_ONLY);
	off_t size;
	if (file.GetSize(&size) != B_OK)
		return 1;
	uint8* data = new uint8[size];
	file.Read(data, size);

	const int sizes[] = { 16, 32, 48, 64, 128 };
	BBitmap* sheet = new BBitmap(BRect(0, 0, 16 + 32 + 48 + 64 + 128 + 60, 139),
		B_BITMAP_NO_SERVER_LINK, B_RGBA32);
	memset(sheet->Bits(), 0xd8, sheet->BitsLength());
	int x = 10;
	for (int i = 0; i < 5; i++) {
		BBitmap icon(BRect(0, 0, sizes[i] - 1, sizes[i] - 1),
			B_BITMAP_NO_SERVER_LINK, B_RGBA32);
		status_t status = BIconUtils::GetVectorIcon(data, size, &icon);
		if (status != B_OK) {
			fprintf(stderr, "render failed at %d: %s\n", sizes[i], strerror(status));
			return 1;
		}
		// composite over the gray sheet
		for (int y = 0; y < sizes[i]; y++) {
			uint8* src = (uint8*)icon.Bits() + y * icon.BytesPerRow();
			uint8* dst = (uint8*)sheet->Bits() + (y + 5) * sheet->BytesPerRow() + x * 4;
			for (int px = 0; px < sizes[i]; px++, src += 4, dst += 4) {
				int a = src[3];
				for (int c = 0; c < 3; c++)
					dst[c] = (src[c] * a + dst[c] * (255 - a)) / 255;
				dst[3] = 255;
			}
		}
		x += sizes[i] + 10;
	}
	BBitmapStream stream(sheet);
	BFile out(argv[2], B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	return BTranslatorRoster::Default()->Translate(&stream, NULL, NULL, &out,
		B_PNG_FORMAT) == B_OK ? 0 : 1;
}
