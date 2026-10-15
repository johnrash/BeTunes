/*
 * Where BeTunes keeps its files: ~/config/settings/BeTunes/
 *   Settings	window layout, volume, shuffle and repeat, columns
 *   Library	songs and playlists
 */
#ifndef SETTINGS_H
#define SETTINGS_H


#include <Message.h>
#include <String.h>


BString settings_path(const char* leaf);
status_t load_message(const char* leaf, BMessage& message);
status_t save_message(const char* leaf, const BMessage& message);


#endif	// SETTINGS_H
