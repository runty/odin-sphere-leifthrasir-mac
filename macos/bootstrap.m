// Native Finder entry point. Rosetta's CPU environment must exist before exec.
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>

static int error(NSString *message, BOOL cli) {
    fprintf(stderr, "Open Odin Sphere: %s\n", message.UTF8String);
    if (!cli) {
        [NSApplication sharedApplication];
        NSAlert *alert = [NSAlert new];
        alert.messageText = @"Odin Sphere could not start";
        alert.informativeText = message;
        [alert runModal];
    }
    return 1;
}

static BOOL availableVolume(NSString *path) {
    NSArray *parts = path.pathComponents;
    if (parts.count < 3 || ![parts[1] isEqual:@"Volumes"]) return YES;
    NSString *volume = [@"/Volumes" stringByAppendingPathComponent:parts[2]];
    struct stat mount, parent;
    return stat(volume.fileSystemRepresentation, &mount) == 0 &&
           stat("/Volumes", &parent) == 0 && mount.st_dev != parent.st_dev;
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        NSFileManager *fm = NSFileManager.defaultManager;
        NSDictionary *env = NSProcessInfo.processInfo.environment;
        BOOL cli = argc > 1;
        NSString *data = env[@"OSL_DATA_DIR"] ?: [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/OdinSphere"];
        if (!data.isAbsolutePath || !availableVolume(data)) return error(@"The data folder must be on an available volume and use an absolute path.", cli);
        if (![fm createDirectoryAtPath:data withIntermediateDirectories:YES attributes:nil error:NULL]) return error(@"Cannot create the Odin Sphere data folder.", cli);
        NSString *configPath = [data stringByAppendingPathComponent:@"installation.plist"];
        if (argc == 4 && !strcmp(argv[1], "--configure")) {
            NSString *app0 = @(argv[2]), *image = @(argv[3]);
            if (!app0.isAbsolutePath || !image.isAbsolutePath || !availableVolume(app0) || !availableVolume(image) ||
                ![fm fileExistsAtPath:[app0 stringByAppendingPathComponent:@"eboot.bin"]] ||
                ![fm fileExistsAtPath:[image stringByAppendingPathComponent:@"boot.bin"]]) return error(@"Configure requires an available prepared game folder and image folder.", YES);
            NSDictionary *config = @{@"app0": app0, @"image": image};
            if (![config writeToFile:configPath atomically:YES]) return error(@"Cannot save the installation paths.", YES);
            printf("Configured %s\n", configPath.fileSystemRepresentation);
            return 0;
        }
        NSDictionary *config = [NSDictionary dictionaryWithContentsOfFile:configPath] ?: @{};
        NSString *app0 = config[@"app0"], *image = config[@"image"];
        if ((app0 && ![app0 isKindOfClass:NSString.class]) || (image && ![image isKindOfClass:NSString.class])) return error(@"The installation paths are invalid.", cli);
        if ((app0 && (!app0.isAbsolutePath || !availableVolume(app0))) ||
            (image && (!image.isAbsolutePath || !availableVolume(image)))) return error(@"The game disk is unavailable. Reconnect it and open Odin Sphere again.", cli);
        NSBundle *bundle = NSBundle.mainBundle;
        NSString *runner = [bundle.bundlePath stringByAppendingPathComponent:@"Contents/MacOS/odin-sphere-runner"];
        NSString *icd = [bundle.resourcePath stringByAppendingPathComponent:@"vulkan/icd.d/kosmickrisp.json"];
        NSString *logs = env[@"OSL_LOGS_DIR"] ?: [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs/OdinSphere"];
        if (!logs.isAbsolutePath || !availableVolume(logs)) return error(@"The log folder is unavailable.", cli);
        if (![fm createDirectoryAtPath:logs withIntermediateDirectories:YES attributes:nil error:NULL]) return error(@"Cannot create the log folder.", cli);
        setenv("ROSETTA_ADVERTISE_AVX", "1", 1);
        setenv("VK_DRIVER_FILES", icd.fileSystemRepresentation, 1);
        setenv("OSL_PORTABLE_DIR", data.fileSystemRepresentation, 1);
        setenv("SDL_AUDIO_DEVICE_SAMPLE_FRAMES", "256", 0);
        NSMutableArray<NSString *> *args = [NSMutableArray arrayWithObject:runner];
        BOOL raw = argc > 1 && !strcmp(argv[1], "--runner");
        if (!raw) {
            if (image) [args addObject:[image stringByAppendingPathComponent:@"boot.bin"]];
            if (app0) [args addObjectsFromArray:@[@"--app0", app0]];
            [args addObjectsFromArray:@[@"--user", [data stringByAppendingPathComponent:@"user"], @"--logs", logs]];
        }
        for (int i = raw ? 2 : 1; i < argc; ++i) [args addObject:@(argv[i])];
        if (argc == 2 && !strcmp(argv[1], "--bootstrap-check")) {
            NSDictionary *report = @{@"runner": runner, @"icd": icd, @"data": data, @"logs": logs,
                                     @"app0": app0 ?: @"", @"image": image ?: @"", @"avx": @YES};
            NSData *json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:NULL];
            fwrite(json.bytes, 1, json.length, stdout); putchar('\n'); return 0;
        }
        if (!raw) {
            // The descriptor survives exec so the running guest owns the lock.
            int lock = open([data stringByAppendingPathComponent:@"instance.lock"].fileSystemRepresentation, O_CREAT | O_RDWR, 0600);
            if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0) return error(@"Odin Sphere is already using this data folder. Quit that copy before starting another.", cli);
        }
        // Capture bootstrap/dyld failures too; the runtime opens its own retained log.
        int fd = open([logs stringByAppendingPathComponent:@"bootstrap.log"].fileSystemRepresentation, O_CREAT | O_WRONLY | O_APPEND, 0600);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
        char **execArgs = calloc(args.count + 1, sizeof(char *));
        for (NSUInteger i = 0; i < args.count; ++i) execArgs[i] = strdup(args[i].fileSystemRepresentation);
        execv(runner.fileSystemRepresentation, execArgs);
        return error([NSString stringWithFormat:@"The bundled runner could not execute (%s). Rosetta 2 is required.", strerror(errno)], cli);
    }
}
