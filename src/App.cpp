/*
 * BeTunes - a music player for Haiku in the spirit of iTunes from the
 * mid 2000s.
 */
#include <AboutWindow.h>
#include <Application.h>
#include <Entry.h>
#include <Path.h>
#include <TextView.h>

#include <math.h>

#include <vector>

#include "BeTunes.h"
#include "EqualizerWindow.h"
#include "MainWindow.h"
#include "Settings.h"


// BAboutWindow gives its text a fixed height and scrolls the rest: makes the
// window tall enough to show all of it.
static void
fit_about_text(BAboutWindow* about)
{
	if (!about->Lock())
		return;
	about->Layout(true);
	BTextView* info = dynamic_cast<BTextView*>(about->FindView("info"));
	if (info != NULL) {
		// BTextView scrolls by its text height plus insets, rounded up,
		// minus the IntegerHeight() of its bounds (one less than the pixels)
		float left, top, right, bottom;
		info->GetInsets(&left, &top, &right, &bottom);
		float needed = ceilf(info->TextRect().Height() + top + bottom);
		float missing = needed - info->Bounds().IntegerHeight();
		if (missing > 0) {
			about->ResizeBy(0, missing);
			BRect frame = about->Frame();
			about->MoveTo(about->AboutPosition(frame.Width(), frame.Height()));
		}
	}
	about->Unlock();
}


class App : public BApplication {
public:
	App()
		:
		BApplication(APP_SIGNATURE),
		fWindow(NULL)
	{
	}

	virtual void ReadyToRun()
	{
		BMessage settings;
		load_message("Settings", settings);
		fWindow = new MainWindow(settings);
		fWindow->Show();

		for (size_t i = 0; i < fPending.size(); i++)
			fWindow->PostMessage(&fPending[i]);
		fPending.clear();
	}

	// Files opened from Tracker: added to the library and played.
	virtual void RefsReceived(BMessage* message)
	{
		BMessage import(kMsgImportPaths);
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++)
			import.AddRef("refs", &ref);
		import.AddBool("play", true);
		_Send(import);
	}

	// The same from the command line.
	virtual void ArgvReceived(int32 argc, char** argv)
	{
		const char* cwd = NULL;
		BMessage* current = CurrentMessage();
		if (current != NULL)
			cwd = current->GetString("cwd", NULL);

		BMessage import(kMsgImportPaths);
		for (int32 i = 1; i < argc; i++) {
			BPath path;
			if (argv[i][0] != '/' && cwd != NULL)
				path.SetTo(cwd, argv[i]);
			else
				path.SetTo(argv[i]);
			entry_ref ref;
			if (path.InitCheck() == B_OK
				&& get_ref_for_path(path.Path(), &ref) == B_OK)
				import.AddRef("refs", &ref);
		}
		if (!import.HasRef("refs"))
			return;
		import.AddBool("play", true);
		_Send(import);
	}

	virtual bool QuitRequested()
	{
		// closing the Equalizer window only hides it, until now
		EqualizerWindow::AllowQuit();
		return BApplication::QuitRequested();
	}

	virtual void AboutRequested()
	{
		BAboutWindow* about = new BAboutWindow(APP_NAME, APP_SIGNATURE);
		about->AddDescription("A music player in the spirit of iTunes from "
			"the mid 2000s: a library, playlists, a browser by artist and "
			"album, search, shuffle and repeat. It plays everything the "
			"installed media plug-ins can decode: MP3, Ogg Vorbis, Opus, "
			"FLAC, AAC, WAV and more.");
		about->AddCopyright(2026, "BeTunes authors");
		fit_about_text(about);
		about->Show();
	}

private:
	void _Send(const BMessage& message)
	{
		if (fWindow != NULL)
			fWindow->PostMessage(const_cast<BMessage*>(&message));
		else
			fPending.push_back(message);
	}

	MainWindow*				fWindow;
	std::vector<BMessage>	fPending;
};


int
main()
{
	App app;
	app.Run();
	return 0;
}
