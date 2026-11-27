// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 OpenTS contributors

#include "gamedatapicker.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cerrno>
#include <copyfile.h>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <future>
#include <mutex>

namespace {
NSString * const DATA_DIRECTORY_KEY = @"GameDataDirectory";


void Show_Import_Error(NSString * detail)
{
	NSAlert * alert = [[NSAlert alloc] init];
	alert.alertStyle = NSAlertStyleWarning;
	alert.messageText = @"Could not import game data";
	alert.informativeText = detail;
	[alert runModal];
	[alert release];
}


NSURL * Choose_Import_Destination(std::string const & default_directory, bool directory)
{
	NSSavePanel * panel = [NSSavePanel savePanel];
	panel.title = @"Import Tiberian Sun Game Data";
	panel.prompt = @"Import";
	panel.message = directory ? @"Choose a destination for the game folder copy."
		: @"Choose a new folder for the archives from the selected ISO files.";
	panel.nameFieldStringValue = @"TiberianSun";
	if (!default_directory.empty()) {
		panel.directoryURL = [NSURL fileURLWithFileSystemRepresentation:default_directory.c_str()
			isDirectory:YES relativeToURL:nil];
	}
	return([panel runModal] == NSModalResponseOK ? panel.URL : nil);
}


struct ImportProgress
{
	std::mutex Mutex;
	int Percent = 0;
	std::string Detail = "Preparing game data";

	void Update(int percent, char const * detail)
	{
		std::lock_guard lock(Mutex);
		Percent = percent;
		Detail = detail;
	}
};


NSString * Run_Importer(NSArray<NSString *> * inputs, NSURL * destination, ImportProgress & progress)
{
	NSURL * importer = [[NSBundle mainBundle] URLForResource:@"import-iso" withExtension:@"sh"];
	if (importer == nil) return(@"The game data importer is missing from OpenTS.app.");

	NSMutableArray<NSString *> * arguments = [NSMutableArray arrayWithObjects:importer.path, destination.path, nil];
	[arguments addObjectsFromArray:inputs];
	NSPipe * output = [NSPipe pipe];
	NSError * error = nil;
	NSTask * task = [[[NSTask alloc] init] autorelease];
	task.executableURL = [NSURL fileURLWithPath:@"/bin/bash"];
	task.arguments = arguments;
	task.standardOutput = output;
	task.standardError = output;
	if (![task launchAndReturnError:&error]) return(error.localizedDescription);

	NSMutableString * errors = [NSMutableString string];
	std::string pending;
	while (true) {
		NSData * chunk = output.fileHandleForReading.availableData;
		if (chunk.length == 0) break;
		pending.append(static_cast<char const *>(chunk.bytes), chunk.length);
		for (std::size_t newline = pending.find('\n'); newline != std::string::npos; newline = pending.find('\n')) {
			std::string line = pending.substr(0, newline);
			pending.erase(0, newline + 1);
			std::size_t separator = line.find('\t', 9);
			if (line.starts_with("PROGRESS\t") && separator != std::string::npos) {
				progress.Update(std::atoi(line.c_str() + 9), line.c_str() + separator + 1);
			} else if (errors.length < 2048) {
				NSString * message = [NSString stringWithUTF8String:line.c_str()];
				if (message != nil) [errors appendFormat:@"%@\n", message];
			}
		}
	}
	[task waitUntilExit];
	if (task.terminationStatus == 0) return(nil);
	NSString * detail = [errors stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
	return(detail.length ? detail : @"The game data importer exited without an error message.");
}


NSString * Import_Data(NSArray<NSURL *> * sources, NSURL * destination, bool directory, ImportProgress & progress)
{
	NSFileManager * files = [NSFileManager defaultManager];
	NSURL * temporary = [NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES];
	NSURL * scratch = [temporary URLByAppendingPathComponent:[NSUUID UUID].UUIDString isDirectory:YES];
	NSError * error = nil;
	if (![files createDirectoryAtURL:scratch withIntermediateDirectories:NO attributes:nil error:&error]) {
		return(error.localizedDescription);
	}

	@try {
		NSArray<NSString *> * inputs;
		if (directory) {
			inputs = @[@"--directory", sources.firstObject.path];
		} else {
			NSMutableArray<NSString *> * staged_images = [NSMutableArray arrayWithCapacity:sources.count];
			for (NSURL * image in sources) {
				progress.Update(static_cast<int>(5 * staged_images.count / sources.count),
					[[NSString stringWithFormat:@"Preparing %@ (%lu of %lu)", image.lastPathComponent,
					(unsigned long)staged_images.count + 1, (unsigned long)sources.count] UTF8String]);
				NSURL * slot = [scratch URLByAppendingPathComponent:
					[NSString stringWithFormat:@"%lu", (unsigned long)staged_images.count] isDirectory:YES];
				if (![files createDirectoryAtURL:slot withIntermediateDirectories:NO attributes:nil error:&error]) {
					return(error.localizedDescription);
				}
				NSURL * staged = [slot URLByAppendingPathComponent:image.lastPathComponent];
				// The disk-image service needs copies it can read outside the picker grant.
				if (copyfile(image.fileSystemRepresentation, staged.fileSystemRepresentation, nullptr,
					COPYFILE_DATA | COPYFILE_CLONE | COPYFILE_EXCL) != 0) {
					int const copy_error = errno;
					return([NSString stringWithFormat:@"Could not copy %@ for import: %s",
						image.lastPathComponent, strerror(copy_error)]);
				}
				[staged_images addObject:staged.path];
			}
			inputs = staged_images;
		}
		NSURL * imported = [scratch URLByAppendingPathComponent:@"data" isDirectory:YES];
		NSString * failure = Run_Importer(inputs, imported, progress);
		if (failure != nil) return(failure);
		progress.Update(99, "Moving game data to its destination");
		return([files moveItemAtURL:imported toURL:destination error:&error] ? nil : error.localizedDescription);
	} @finally {
		[files removeItemAtURL:scratch error:nullptr];
	}
}


bool Show_Import_Progress(NSArray<NSURL *> * sources, NSURL * destination, bool directory)
{
	NSWindow * window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 496, 128)
		styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
	window.releasedWhenClosed = NO;
	window.title = @"Importing Game Data";
	NSTextField * detail = [NSTextField labelWithString:@""];
	detail.frame = NSMakeRect(22, 86, 452, 20);
	detail.lineBreakMode = NSLineBreakByTruncatingMiddle;
	[window.contentView addSubview:detail];
	NSProgressIndicator * bar = [[[NSProgressIndicator alloc] initWithFrame:NSMakeRect(22, 51, 452, 18)] autorelease];
	bar.indeterminate = NO;
	bar.maxValue = 100;
	[window.contentView addSubview:bar];
	NSTextField * elapsed = [NSTextField labelWithString:@""];
	elapsed.frame = NSMakeRect(22, 14, 452, 20);
	elapsed.textColor = NSColor.secondaryLabelColor;
	[window.contentView addSubview:elapsed];

	ImportProgress progress;
	auto start = std::chrono::steady_clock::now();
	auto worker = std::async(std::launch::async, [&]() -> NSString * {
		@autoreleasepool {
			return([Import_Data(sources, destination, directory, progress) copy]);
		}
	});

	[window center];
	[window makeKeyAndOrderFront:nil];
	NSModalSession session = [NSApp beginModalSessionForWindow:window];
	do {
		@autoreleasepool {
			int percent;
			std::string message;
			{
				std::lock_guard lock(progress.Mutex);
				percent = progress.Percent;
				message = progress.Detail;
			}
			detail.stringValue = [NSString stringWithUTF8String:message.c_str()] ?: @"Importing game data";
			bar.doubleValue = percent;
			auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
			elapsed.stringValue = [NSString stringWithFormat:@"%d%% complete · %lld:%02lld elapsed",
				percent, static_cast<long long>(seconds / 60), static_cast<long long>(seconds % 60)];
			[NSApp runModalSession:session];
			[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		}
	} while (worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
	NSString * error = worker.get();
	[NSApp endModalSession:session];
	[window close];
	[window release];
	if (error != nil) {
		Show_Import_Error(error);
		[error release];
		return(false);
	}
	return(true);
}


NSModalResponse Choose_Folder_Action()
{
	NSAlert * alert = [[[NSAlert alloc] init] autorelease];
	alert.messageText = @"Use the selected game folder?";
	alert.informativeText = @"Use the folder where it is, or copy it to a new location.";
	[alert addButtonWithTitle:@"Use in Place"];
	[alert addButtonWithTitle:@"Import Copy…"];
	[alert addButtonWithTitle:@"Cancel"];
	return([alert runModal]);
}

}


std::string Mac_Stored_Data_Directory()
{
	@autoreleasepool {
		NSString * path = [[NSUserDefaults standardUserDefaults] stringForKey:DATA_DIRECTORY_KEY];
		char const * text = [path fileSystemRepresentation];
		return(text != nullptr ? text : "");
	}
}


void Mac_Store_Data_Directory(std::string const & path)
{
	@autoreleasepool {
		NSString * text = [NSString stringWithUTF8String:path.c_str()];
		if (text != nil) {
			[[NSUserDefaults standardUserDefaults] setObject:text forKey:DATA_DIRECTORY_KEY];
		}
	}
}


std::string Mac_Choose_Data_Directory(std::string const & default_directory)
{
	@autoreleasepool {
		[NSApplication sharedApplication];
		while (true) {
			NSOpenPanel * panel = [NSOpenPanel openPanel];
			panel.title = @"Locate Game Data";
			panel.prompt = @"Continue";
			panel.message = @"Select a Tiberian Sun folder or select one or more ISO files to import together.";
			panel.canChooseFiles = YES;
			panel.canChooseDirectories = YES;
			panel.allowsMultipleSelection = YES;
			panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"iso"]];
			if ([panel runModal] != NSModalResponseOK) return(std::string());

			NSArray<NSURL *> * selection = panel.URLs;
			NSUInteger directories = 0;
			for (NSURL * item in selection) {
				NSNumber * is_directory = nil;
				[item getResourceValue:&is_directory forKey:NSURLIsDirectoryKey error:nullptr];
				if (is_directory.boolValue) directories++;
			}

			NSURL * folder;
			if (directories == 0) {
				folder = Choose_Import_Destination(default_directory, false);
				if (folder == nil || !Show_Import_Progress(selection, folder, false)) continue;
			} else if (selection.count == 1) {
				NSModalResponse action = Choose_Folder_Action();
				if (action == NSAlertFirstButtonReturn) {
					folder = selection.firstObject;
				} else if (action == NSAlertSecondButtonReturn) {
					folder = Choose_Import_Destination(default_directory, true);
					if (folder == nil || !Show_Import_Progress(selection, folder, true)) continue;
				} else {
					continue;
				}
			} else {
				Show_Import_Error(@"Select one game folder or select only ISO files.");
				continue;
			}

			char const * path = [folder fileSystemRepresentation];
			return(path != nullptr ? path : "");
		}
	}
}
