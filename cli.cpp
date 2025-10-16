#include "wchar_util.h"
#include "xp3.h"
#if _WIN32
#include <Windows.h>
#endif
#include "fileop.h"
#include <unordered_map>
#include <inttypes.h>

int main(int argc, char* argv[]) {
#if _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::vector<std::string> args;
#if _WIN32
    if (!wchar_util::getArgv(args)) {
        for (int i = 0; i < argc; i++) {
            args.push_back(argv[i]);
        }
    }
#else
    for (int i = 0; i < argc; i++) {
        args.push_back(argv[i]);
    }
#endif
    if (args.size() < 3) {
        printf("Usage: %s ls/extract <xp3 file>\n", args[0].c_str());
        return 1;
    }
    std::string action = args[1];
    std::string xp3file = args[2];
    if (action == "ls") {
        Xp3Archive archive(xp3file.c_str());
        if (!archive.ReadIndex()) {
            printf("Failed to read index from %s\n", xp3file.c_str());
            return 1;
        }
        std::unordered_map<uint64_t, uint64_t> seg_counter;
        for (const auto& file : archive.files) {
            for (const auto& seg : file.segments) {
                seg_counter[seg.start]++;
            }
        }
        for (const auto& file : archive.files) {
            printf("%s (original size: %" PRIu64 ", packed size: %" PRIu64 ", segments: %zu)\n", file.filename.c_str(), file.original_size, file.packed_size, file.segments.size());
            for (const auto& seg : file.segments) {
                printf("  Segment: start=%" PRIu64 ", original_size=%" PRIu64 ", packed_size=%" PRIu64 ", flag=0x%X, count=%" PRIu64 "\n", seg.start, seg.original_size, seg.packed_size, seg.flag, seg_counter[seg.start]);
            }
        }
    } else if (action == "extract") {
        const size_t chunk_size = 8192;
        uint8_t buffer[chunk_size];
        Xp3Archive archive(xp3file.c_str());
        if (!archive.ReadIndex()) {
            printf("Failed to read index from %s\n", xp3file.c_str());
            return 1;
        }
        for (const auto& file: archive.files) {
            printf("Extracting %s ... ", file.filename.c_str());
            Xp3File* inf = archive.OpenFile(file);
            if (!inf) {
                printf("Failed to open file %s\n", file.filename.c_str());
                continue;
            }
            std::string filename = fileop::join(fileop::filename(xp3file), file.filename);
            fileop::mkdir_for_file(filename, 0);
            FILE* outfp = fileop::fopen(filename, "wb");
            if (!outfp) {
                printf("Failed to open output file %s\n", filename.c_str());
                delete inf;
                continue;
            }
            uint64_t total_written = 0;
            while (true) {
                size_t r = inf->read(buffer, chunk_size);
                if (r == 0) break;
                size_t written = fwrite(buffer, 1, r, outfp);
                if (written != r) {
                    printf("Failed to write to output file %s\n", filename.c_str());
                    break;
                }
                total_written += written;
            }
            fclose(outfp);
            if (total_written != file.original_size) {
                printf("Warning: extracted size (%" PRIu64 ") does not match original size (%" PRIu64 ")\n", total_written, file.original_size);
            } else {
                printf("Done (%" PRIu64 " bytes)\n", total_written);
            }
            delete inf;
        }
    } else {
        printf("Unknown action: %s\n", action.c_str());
        return 1;
    }
    return 0;
}
