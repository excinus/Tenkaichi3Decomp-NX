// The Switch setup's work (port/switch/setup/setup.cpp is its window; docs/port/switch_plan.md, "The setup"): puts the
// game on a Switch's SD card, with the game data from the player's own disc, in one of two ways:
//
//   from the disc image   the image read and checked as the PC setup does (port/setup/native.cpp, compiled in here:
//                         its ISO reader, the checksums of the USA release, the European languages), and its files
//                         written to the card: the loose files to gamedata/disc/, each archive split as the PC's
//                         gamedata has it, except the large ones (PZS3US2.AFS, the voices: about 65,000 files), which
//                         go to the card whole, as gamedata/<name>.afs: the disc's archive is the pack the Switch
//                         build reads (port/src/plat_nxfs.c, "The packs");
//   from the PC version   its gamedata copied to the card, each folder of 10,000 files or more written as one pack
//                         instead (as port/switch/pack_gamedata.py does), checked against the folder.
// And in both: the game and its launcher (atmosphere/... and switch/Tenkaichi3Decomp.nro), from the release's SD zip
// carried at the end of this program (port/switch/setup/build.sh appends it, with a trailer that says where it is).
// Files already on the card and the same size are kept, so a second run continues where one stopped.
//
// It reports through native.cpp's event lines (@step, @note, @ok, @skip, @fail, @stopped, @done), read by the window.
#include "../../setup/native.cpp"

#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#include "../../third_party/miniz/miniz.h"
#include "../../third_party/miniz/miniz.c"
#include "work.h"

#ifdef _WIN32
extern "C" {
__declspec(dllimport) int __stdcall GetDiskFreeSpaceExW(const wchar_t *dir, unsigned long long *avail, unsigned long long *total,
                                                        unsigned long long *free);
__declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned page, unsigned long flags, const char *s, int n, wchar_t *out, int outLen);
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(void *module, wchar_t *out, unsigned long size);
__declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned page, unsigned long flags, const wchar_t *wide, int wideLen, char *out, int outLen,
                                                        const char *def, int *usedDef);
}
#else
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace {

const char *kGameDir = "atmosphere/contents/0100B73DEC000000/";
const Uint32 kPackFrom = 10000; // a folder of this many files or more goes to the card as one pack
const Uint32 kAlign = 2048;

std::string slash(std::string s) {
    for (auto &c : s) {
        if (c == '\\') {
            c = '/';
        }
    }
    if (!s.empty() && s.back() != '/') {
        s += '/';
    }
    return s;
}

bool is_dir(const std::string &p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

Sint64 file_size(const std::string &p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_FILE ? (Sint64)info.size : -1;
}

void make_dir(const std::string &p) {
    if (!SDL_CreateDirectory(p.c_str())) { // (SDL makes the folders above it too)
        throw Stop{"Could not make the folder " + p + " on the SD card. Is it write-protected?"};
    }
}

// a file written beside its place and then put there: a stopped run never leaves half a file under the real name
void put_file(const std::string &path, const void *data, size_t size) {
    std::string part = path + ".part";
    SDL_IOStream *out = SDL_IOFromFile(part.c_str(), "wb");
    bool ok = out != NULL && SDL_WriteIO(out, data, size) == size;
    ok = out != NULL && SDL_CloseIO(out) && ok;
    if (!ok || !SDL_RenamePath(part.c_str(), path.c_str())) {
        SDL_RemovePath(part.c_str());
        throw Stop{"Could not write " + path + ". Is the SD card full or write-protected?"};
    }
}

std::string mb(Uint64 bytes) {
    char s[32];
    snprintf(s, sizeof(s), bytes >= (1ull << 30) ? "%.1f GB" : "%.0f MB", bytes >= (1ull << 30) ? bytes / 1073741824.0 : bytes / 1048576.0);
    return s;
}

// ---- the release carried at the end of this program ----
// The last 16 bytes: "BT3NXSD1" and the zip's size (little-endian); the zip right before them.
bool own_path(std::string &out) {
#ifdef _WIN32
    wchar_t w[1024];
    unsigned long n = GetModuleFileNameW(NULL, w, 1024);
    char u[4096];
    if (n == 0 || n >= 1024 || WideCharToMultiByte(65001, 0, w, -1, u, sizeof(u), NULL, NULL) <= 0) {
        return false;
    }
    out = u;
    return true;
#else
    char u[4096];
    ssize_t n = readlink("/proc/self/exe", u, sizeof(u) - 1);
    if (n <= 0) {
        return false;
    }
    u[n] = '\0';
    out = u;
    return true;
#endif
}

bool payload(std::vector<Uint8> &zip) {
    std::string self;
    const char *env = getenv("BT3_NXSETUP_ZIP"); // (testing: the SD zip as a file instead)
    if (env != NULL) {
        size_t n = 0;
        void *d = SDL_LoadFile(env, &n);
        if (d == NULL) {
            return false;
        }
        zip.assign((Uint8 *)d, (Uint8 *)d + n);
        SDL_free(d);
        return true;
    }
    if (!own_path(self)) {
        return false;
    }
    SDL_IOStream *io = SDL_IOFromFile(self.c_str(), "rb");
    Uint8 tail[16];
    Sint64 size = io != NULL ? SDL_GetIOSize(io) : -1;
    bool ok = size > 16 && SDL_SeekIO(io, size - 16, SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(io, tail, 16) == 16 && memcmp(tail, "BT3NXSD1", 8) == 0;
    if (ok) {
        Uint64 n = 0;
        for (int i = 7; i >= 0; i--) {
            n = n << 8 | tail[8 + i];
        }
        ok = n > 0 && (Sint64)n <= size - 16;
        if (ok) {
            zip.resize((size_t)n);
            ok = SDL_SeekIO(io, size - 16 - (Sint64)n, SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(io, zip.data(), zip.size()) == zip.size();
        }
    }
    if (io != NULL) {
        SDL_CloseIO(io);
    }
    return ok;
}

void step_switch(const std::string &sd) {
    std::vector<Uint8> zip;
    mz_zip_archive z;
    int n, files = 0;

    step("Switch files");
    if (!payload(zip)) {
        throw Stop{"This setup does not carry the game's files (it is not a release's Tenkaichi3Decomp-switch-setup.exe).\n"
                   "Download the setup again from the releases page."};
    }
    memset(&z, 0, sizeof(z));
    if (!mz_zip_reader_init_mem(&z, zip.data(), zip.size(), 0)) {
        throw Stop{"The game's files inside this setup are damaged. Download the setup again."};
    }
    n = (int)mz_zip_reader_get_num_files(&z);
    try {
        for (int i = 0; i < n; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&z, (mz_uint)i, &st) || st.m_is_directory) {
                continue;
            }
            std::string name = st.m_filename;
            if ((name.rfind("atmosphere/", 0) != 0 && name.rfind("switch/", 0) != 0) || name.find("..") != std::string::npos) {
                continue; // (the zip's notes and tools for installing by hand)
            }
            size_t size = 0;
            void *data = mz_zip_reader_extract_to_heap(&z, (mz_uint)i, &size, 0);
            if (data == NULL) {
                throw Stop{"The game's files inside this setup are damaged. Download the setup again."};
            }
            std::string path = sd + name;
            try {
                make_dir(path.substr(0, path.rfind('/')));
                put_file(path, data, size);
            } catch (...) {
                mz_free(data);
                throw;
            }
            mz_free(data);
            files++;
            check_cancel();
        }
    } catch (...) {
        mz_zip_reader_end(&z);
        throw;
    }
    mz_zip_reader_end(&z);
    char line[96];
    snprintf(line, sizeof(line), "@ok the game and its launcher, %d files", files);
    say(line);
}

// ---- free space on the card ----
Uint64 free_space(const std::string &dir) {
#ifdef _WIN32
    wchar_t w[1024];
    unsigned long long avail = 0, total = 0, fr = 0;
    if (MultiByteToWideChar(65001, 0, dir.c_str(), -1, w, 1024) > 0 && GetDiskFreeSpaceExW(w, &avail, &total, &fr)) {
        return avail;
    }
    return 0;
#else
    struct statvfs v;
    return statvfs(dir.c_str(), &v) == 0 ? (Uint64)v.f_bavail * v.f_frsize : 0;
#endif
}

void need_space(const std::string &sd, Uint64 bytes) {
    Uint64 have = free_space(sd);
    if (have != 0 && have + (64ull << 20) < bytes) {
        throw Stop{"The SD card has " + mb(have) + " free; the game data needs about " + mb(bytes) + "."};
    }
}

// ---- progress ----
struct Progress {
    Uint64 total = 0, done = 0;
    int files = 0, last = -1;
    void add(Uint64 n) {
        done += n;
        int percent = total ? (int)(done * 100 / total) : 0;
        if (percent != last) {
            char line[64];
            snprintf(line, sizeof(line), "@note %d%%, %d files", percent, files);
            say(line);
            last = percent;
        }
        check_cancel();
    }
};

// copies `size` bytes of the image at `offset` to `path` unless a file of that size is there
void iso_file(Iso &iso, Uint64 offset, Uint64 size, const std::string &path, std::vector<Uint8> &buf) {
    if (file_size(path) == (Sint64)size) {
        return;
    }
    copy_out(iso, offset, size, path + ".part", buf);
    if (!SDL_RenamePath((path + ".part").c_str(), path.c_str())) {
        throw Stop{"Could not write " + path + ". Is the SD card full or write-protected?"};
    }
}

// ---- way 1: from the disc image ----
void step_data_iso(Iso &iso, const std::string &game) {
    std::string data = game + "gamedata/";
    std::vector<const IsoFile *> take;
    Progress p;
    std::vector<Uint8> buf(1 << 20);

    step("Game data");
    for (auto &f : iso.files) {
        std::string u = upper(f.path);
        if (u == "SLUS_216.78" || u.rfind("BIN/", 0) == 0 || u.rfind("DATA/", 0) == 0) {
            take.push_back(&f);
            p.total += f.size;
        }
    }
    need_space(game, p.total);
    for (const IsoFile *f : take) {
        std::string u = upper(f->path);
        bool afs = u.rfind("DATA/", 0) == 0 && u.size() > 4 && u.compare(u.size() - 4, 4, ".AFS") == 0;
        if (!afs) {
            std::string out = data + "disc/" + u;
            make_dir(out.substr(0, out.rfind('/')));
            iso_file(iso, f->offset, f->size, out, buf);
            p.files++;
            p.add(f->size);
            continue;
        }
        std::string stem = lower(u.substr(5, u.size() - 9));
        Uint8 head[8];
        Uint32 count;
        iso.read(f->offset, head, 8);
        memcpy(&count, head + 4, 4);
        if (memcmp(head, "AFS\0", 4) != 0 || (Uint64)count * 8 + 8 > f->size) {
            throw Stop{"An archive on the disc (" + f->path + ") is damaged."};
        }
        if (count >= kPackFrom) { // the whole archive: the pack
            say("@note " + stem + ": " + std::to_string(count) + " files as one pack");
            make_dir(data);
            iso_file(iso, f->offset, f->size, data + stem + ".afs", buf);
            p.files++;
            p.add(f->size);
            continue;
        }
        std::string dir = data + stem + "/";
        std::vector<Uint32> table((size_t)count * 2);
        iso.read(f->offset + 8, table.data(), table.size() * 4);
        make_dir(dir);
        Uint64 inArchive = 0;
        for (Uint32 i = 0; i < count; i++) {
            char name[32];
            snprintf(name, sizeof(name), "%05u.bin", i);
            if ((Uint64)table[i * 2] + table[i * 2 + 1] > f->size) {
                throw Stop{"An archive on the disc (" + f->path + ") is damaged."};
            }
            iso_file(iso, f->offset + table[i * 2], table[i * 2 + 1], dir + name, buf);
            p.files++;
            inArchive += table[i * 2 + 1];
            p.add(table[i * 2 + 1]);
        }
        p.add(f->size > inArchive ? f->size - inArchive : 0);
    }
    put_file(data + ".installed", "", 0);
    say("@ok " + std::to_string(p.files) + " files, " + mb(p.total));
}

// ---- way 2: from the PC version ----
struct Entry { std::string rel; Uint64 size; };

// the names in one folder (not below it)
SDL_EnumerationResult SDLCALL name_of(void *user, const char *dir, const char *name) {
    (void)dir;
    ((std::vector<std::string> *)user)->push_back(name);
    return SDL_ENUM_CONTINUE;
}

std::vector<std::string> names_in(const std::string &dir) {
    std::vector<std::string> out;
    SDL_EnumerateDirectory(dir.c_str(), name_of, &out);
    return out;
}

void list_files(const std::string &root, const std::string &rel, std::vector<Entry> &out) {
    for (auto &name : names_in(root + rel)) {
        std::string r = rel + name;
        SDL_PathInfo info;
        if (!SDL_GetPathInfo((root + r).c_str(), &info)) {
            continue;
        }
        if (info.type == SDL_PATHTYPE_DIRECTORY) {
            list_files(root, r + "/", out);
        } else if (info.type == SDL_PATHTYPE_FILE && r.size() > 0 &&
                   (r.size() < 5 || r.compare(r.size() - 5, 5, ".part") != 0)) {
            out.push_back({r, info.size});
        }
    }
}

// a folder's numbered files (00000.bin ...), when it has kPackFrom of them or more: index -> size
bool pack_folder(const std::string &dir, std::vector<Sint64> &sizes) {
    std::vector<std::pair<Uint32, Sint64>> found;
    for (auto &name : names_in(dir)) {
        const char *s = name.c_str();
        size_t len = strlen(s), k;
        for (k = 0; k < len && isdigit((unsigned char)s[k]); k++) {
        }
        if (k == 0 || k > 6 || strcmp(s + k, ".bin") != 0) {
            continue;
        }
        found.push_back({(Uint32)strtoul(s, NULL, 10), file_size(dir + s)});
    }
    if (found.size() < kPackFrom) {
        return false;
    }
    Uint32 count = 0;
    for (auto &f : found) {
        count = f.first + 1 > count ? f.first + 1 : count;
    }
    sizes.assign(count, -1);
    for (auto &f : found) {
        sizes[f.first] = f.second;
    }
    return true;
}

Uint64 align(Uint64 v) { return (v + kAlign - 1) / kAlign * kAlign; }

// the folder as one AFS archive (pack_gamedata.py's layout), checked, then given its name
void write_pack(const std::string &dir, const std::vector<Sint64> &sizes, const std::string &dest, Progress &p) {
    Uint32 count = (Uint32)sizes.size();
    Uint64 head = align(8 + (Uint64)count * 8), at = head, expect = head;
    for (Sint64 s : sizes) {
        expect += s > 0 ? align((Uint64)s) : 0;
    }
    if (file_size(dest) == (Sint64)expect) { // (the same pack there already)
        for (Sint64 s : sizes) {
            p.files += s >= 0;
            p.add(s > 0 ? (Uint64)s : 0);
        }
        return;
    }
    std::string part = dest + ".part";
    std::vector<Uint32> table((size_t)count * 2, 0);
    std::vector<Uint8> zero(kAlign, 0);
    SDL_IOStream *out = SDL_IOFromFile(part.c_str(), "wb");
    if (out == NULL) {
        throw Stop{"Could not write " + dest + ". Is the SD card full or write-protected?"};
    }
    auto fail = [&](const std::string &why) {
        SDL_CloseIO(out);
        SDL_RemovePath(part.c_str());
        throw Stop{why};
    };
    std::vector<Uint8> pad(head, 0);
    if (SDL_WriteIO(out, pad.data(), pad.size()) != pad.size()) {
        fail("Could not write " + dest + ". Is the SD card full?");
    }
    for (Uint32 i = 0; i < count; i++) {
        if (sizes[i] < 0) {
            continue;
        }
        char name[32];
        snprintf(name, sizeof(name), "%05u.bin", i);
        size_t n = 0;
        void *d = SDL_LoadFile((dir + name).c_str(), &n);
        if (d == NULL && sizes[i] > 0) {
            fail("Could not read " + dir + name + ".");
        }
        if (at + n >= (1ull << 32)) {
            SDL_free(d);
            fail(dir + " is over 4 GB, more than one pack holds.");
        }
        bool ok = n == 0 || SDL_WriteIO(out, d, n) == n;
        SDL_free(d);
        Uint64 padding = align(at + n) - (at + n);
        ok = ok && (padding == 0 || SDL_WriteIO(out, zero.data(), (size_t)padding) == padding);
        if (!ok) {
            fail("Could not write " + dest + ". Is the SD card full?");
        }
        table[i * 2] = (Uint32)at;
        table[i * 2 + 1] = (Uint32)n;
        at = align(at + n);
        p.files++;
        p.add(n);
    }
    Uint8 magic[8] = {'A', 'F', 'S', 0};
    memcpy(magic + 4, &count, 4);
    if (SDL_SeekIO(out, 0, SDL_IO_SEEK_SET) < 0 || SDL_WriteIO(out, magic, 8) != 8 ||
        SDL_WriteIO(out, table.data(), table.size() * 4) != table.size() * 4 || !SDL_CloseIO(out)) {
        SDL_RemovePath(part.c_str());
        throw Stop{"Could not write " + dest + ". Is the SD card full?"};
    }
    // the check: every file's size, every 250th file's bytes
    SDL_IOStream *in = SDL_IOFromFile(part.c_str(), "rb");
    int k = 0;
    for (Uint32 i = 0; in != NULL && i < count; i++) {
        if (sizes[i] < 0) {
            continue;
        }
        if (table[i * 2] == 0 || (Sint64)table[i * 2 + 1] != sizes[i]) {
            SDL_CloseIO(in);
            in = NULL;
            break;
        }
        if (k++ % 250 == 0 && sizes[i] > 0) {
            char name[32];
            snprintf(name, sizeof(name), "%05u.bin", i);
            size_t n = 0;
            void *want = SDL_LoadFile((dir + name).c_str(), &n);
            std::vector<Uint8> got(n);
            bool same = want != NULL && SDL_SeekIO(in, table[i * 2], SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(in, got.data(), n) == n &&
                        memcmp(got.data(), want, n) == 0;
            SDL_free(want);
            if (!same) {
                SDL_CloseIO(in);
                in = NULL;
                break;
            }
        }
    }
    if (in == NULL) {
        SDL_RemovePath(part.c_str());
        throw Stop{"The pack written to the SD card (" + dest + ") does not read back as it was written. The card may be failing."};
    }
    SDL_CloseIO(in);
    if (!SDL_RenamePath(part.c_str(), dest.c_str())) {
        SDL_RemovePath(part.c_str());
        throw Stop{"Could not write " + dest + "."};
    }
}

std::string pc_gamedata(const std::string &pc) {
    std::string root = slash(pc);
    if (file_size(root + "disc/SLUS_216.78") > 0) {
        return root; // (the gamedata folder itself was chosen)
    }
    return root + "gamedata/";
}

void step_pc(const std::string &pc) {
    step("PC installation");
    std::string data = pc_gamedata(pc);
    if (file_size(data + "disc/SLUS_216.78") <= 0) {
        throw Stop{"No game data in " + pc + ".\nChoose the folder of your PC installation of Tenkaichi3Decomp: the one with a gamedata folder "
                   "in it (its setup made it from your disc)."};
    }
    say("@ok " + data);
}

void step_data_pc(const std::string &pc, const std::string &game) {
    std::string src = pc_gamedata(pc), dst = game + "gamedata/";
    std::vector<Entry> files;
    std::vector<std::string> packs;
    Progress p;

    step("Game data");
    say("@note looking at the PC's files");
    for (auto &name : names_in(src)) {
        std::vector<Sint64> sizes;
        if (name.rfind("pzs3", 0) == 0 && is_dir(src + name) && pack_folder(src + name + "/", sizes)) {
            packs.push_back(name);
        }
    }
    list_files(src, "", files);
    std::vector<Entry> copy;
    for (auto &f : files) {
        bool packed = false;
        for (auto &k : packs) {
            packed = packed || f.rel.rfind(k + "/", 0) == 0;
        }
        if (f.rel.rfind(".selftest/", 0) == 0) {
            continue; // (the PC setup's self-test, not the game's data)
        }
        p.total += f.size;
        if (!packed) {
            copy.push_back(f);
        }
    }
    need_space(game, p.total);
    for (auto &f : copy) {
        std::string from = src + f.rel, to = dst + f.rel;
        if (file_size(to) != (Sint64)f.size) {
            size_t size = 0;
            void *d = SDL_LoadFile(from.c_str(), &size);
            if (d == NULL) {
                throw Stop{"Could not read " + from + "."};
            }
            try {
                make_dir(to.substr(0, to.rfind('/')));
                put_file(to, d, size);
            } catch (...) {
                SDL_free(d);
                throw;
            }
            SDL_free(d);
        }
        p.files++;
        p.add(f.size);
    }
    for (auto &k : packs) {
        std::vector<Sint64> sizes;
        pack_folder(src + k + "/", sizes);
        say("@note " + k + ": " + std::to_string(sizes.size()) + " files into one pack");
        make_dir(dst);
        write_pack(src + k + "/", sizes, dst + k + ".afs", p);
    }
    say("@ok " + std::to_string(p.files) + " files, " + mb(p.total));
}

void work_nx(NxJob job) {
    try {
        std::string sd = slash(job.sd), game = sd + kGameDir;
        sStep = 0;
        if (job.fromPc) {
            sSteps = 3;
            step_switch(sd);
            check_cancel();
            step_pc(job.pc);
            check_cancel();
            step_data_pc(job.pc, game);
        } else {
            sSteps = 3 + (job.eu.empty() ? 0 : 1);
            step_switch(sd);
            check_cancel();
            Iso iso;
            if (getenv("BT3_NXSETUP_NOCHECK") != NULL) { // (testing with a made-up image: no checksums)
                step("Disc image");
                iso.open(job.iso);
                say("@ok not checked (BT3_NXSETUP_NOCHECK)");
            } else {
                step_disc(iso, job.iso);
            }
            check_cancel();
            step_data_iso(iso, game);
            check_cancel();
            if (!job.eu.empty()) {
                step_eu(game, job.eu);
            }
        }
        say("@done " + game);
    } catch (const Stop &s) {
        std::string why = s.why;
        for (size_t i = 0; (i = why.find('\n', i)) != std::string::npos;) {
            why.replace(i, 1, "\\n");
        }
        say("@fail");
        say("@stopped " + why);
    }
    sRunning = false;
}

} // namespace

void NxSetup_Start(const NxJob &job) {
    if (sThread.joinable()) {
        sThread.join();
    }
    sCancel = false;
    sRunning = true;
    sLines.clear();
    sThread = std::thread(work_nx, job);
}

bool NxSetup_HasFiles(void) {
    std::vector<Uint8> zip;
    return payload(zip);
}

bool NxSetup_PcValid(const std::string &pc) {
    return !pc.empty() && file_size(pc_gamedata(pc) + "disc/SLUS_216.78") > 0;
}

std::string NxSetup_CardNote(const std::string &sd, bool &warn) {
    std::string root = slash(sd);
    warn = false;
    if (sd.empty() || !is_dir(root)) {
        warn = true;
        return sd.empty() ? "" : "That folder does not exist.";
    }
    Uint64 have = free_space(root);
    std::string space = have ? mb(have) + " free" : "";
    if (!is_dir(root + "atmosphere")) {
        warn = true;
        return "No atmosphere folder here: is this the Switch's SD card, with Atmosphere on it? Choose the card itself (its drive, such as E:\\)." +
               (space.empty() ? "" : " (" + space + ")");
    }
    return "Atmosphere found" + (space.empty() ? std::string(".") : ", " + space + ".");
}
