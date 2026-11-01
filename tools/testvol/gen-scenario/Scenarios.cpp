#include "Scenarios.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <stdexcept>

namespace icd::testvol {

    namespace {

        constexpr std::uint64_t KB = 1024;
        constexpr std::uint64_t MB = 1024 * KB;
        constexpr std::uint64_t GB = 1024 * MB;

        // How sizes of one kind of file are drawn.
        struct Sizing {
            double median; // Typical size in bytes.
            double sigma; // Log-normal spread.
            std::uint64_t lo; // Smallest size.
            std::uint64_t hi; // Largest size.
        };

        // Shape of a generated folder tree.
        struct TreeShape {
            std::span<const std::wstring_view> extensions; // Extensions picked at random.
            int perDir = 40; // Files per leaf directory (and leaves per parent).
            Content content = Content::Random; // Byte pattern of the files.
        };

        // Appends one batch to another.
        void addAll(std::vector<FileSpec>& to, const std::vector<FileSpec>& from) {
            to.insert(to.end(), from.begin(), from.end());
        }

        // A size drawn from the sizing.
        std::uint64_t drawSize(Generator& gen, const Sizing& s) {
            return logNormalSize(gen.rng(), s.median, s.sigma, s.lo, s.hi);
        }

        // Specs for files totalling about `bytes` in a two-level tree under dir; nothing is written yet.
        std::vector<FileSpec> makeTree(Generator& gen, const std::wstring& dir, std::uint64_t bytes, const Sizing& s,
                                       const TreeShape& shape) {
            std::vector<FileSpec> specs;
            std::uint64_t total = 0;
            for (int n = 0; total < bytes; ++n) {
                const int leaf = n / shape.perDir;
                const std::wstring_view ext = shape.extensions[gen.rng().below(shape.extensions.size())];
                FileSpec spec{
                    std::format(L"{}\\p{}\\d{}\\f{}.{}", dir, leaf / shape.perDir, leaf % shape.perDir, n, ext),
                    std::min(drawSize(gen, s), bytes - total), gen.nextSeed(), shape.content};
                total += spec.size;
                specs.push_back(std::move(spec));
            }
            return specs;
        }

        // Specs for `count`-limited big files named dir\<prefix>-<n>.<ext> totalling about `bytes`.
        std::vector<FileSpec> makeFlat(Generator& gen, const std::wstring& dir, std::uint64_t bytes, const Sizing& s,
                                       std::span<const std::wstring_view> extensions) {
            std::vector<FileSpec> specs;
            std::uint64_t total = 0;
            for (int n = 0; total < bytes; ++n) {
                const std::wstring_view ext = extensions[gen.rng().below(extensions.size())];
                FileSpec spec{std::format(L"{}\\item-{}.{}", dir, n, ext), std::min(drawSize(gen, s), bytes - total),
                              gen.nextSeed()};
                total += spec.size;
                specs.push_back(std::move(spec));
            }
            return specs;
        }

        // Clamps a sizing to a share of the scenario budget, so small volumes still get several files.
        Sizing scaled(Sizing s, std::uint64_t bytes, std::uint64_t parts) {
            s.hi = std::max<std::uint64_t>(std::min(s.hi, bytes / parts), 1);
            s.lo = std::min(s.lo, s.hi);
            s.median = std::min(s.median, static_cast<double>(s.hi) / 2);
            return s;
        }

        // Splits a vector into batches of about `size` elements.
        std::vector<std::vector<FileSpec>> batches(const std::vector<FileSpec>& specs, std::size_t size) {
            std::vector<std::vector<FileSpec>> out;
            for (std::size_t i = 0; i < specs.size(); i += size) {
                out.emplace_back(specs.begin() + static_cast<std::ptrdiff_t>(i),
                                 specs.begin() + static_cast<std::ptrdiff_t>(std::min(specs.size(), i + size)));
            }
            return out;
        }

        // --- dev -------------------------------------------------------------------------------------

        constexpr std::array<std::wstring_view, 6> kSourceExt{L"cpp", L"h", L"py", L"js", L"md", L"json"};
        constexpr std::array<std::wstring_view, 3> kModuleExt{L"js", L"json", L"map"};
        constexpr std::array<std::wstring_view, 3> kObjectExt{L"obj", L"pdb", L"lib"};

        // Sources written in batches with the pack file growing in between (commits), like git history.
        std::vector<FileSpec> devSources(Generator& gen, const std::wstring& repo, std::uint64_t srcBytes,
                                         std::uint64_t packBytes) {
            const std::wstring pack = repo + L"\\.git\\objects\\pack\\pack-1.pack";
            std::vector<FileSpec> kept = gen.write({{pack, packBytes / 10, gen.nextSeed()}}, {});
            const auto specs = makeTree(gen, repo + L"\\src", srcBytes, {6.0 * KB, 1.2, 100, 512 * KB},
                                        {kSourceExt, 30, Content::Text});
            const auto parts = batches(specs, 40);
            for (const auto& batch : parts) {
                addAll(kept, gen.write(batch, {1, 64 * KB}));
                gen.append(pack, packBytes * 9 / 10 / parts.size());
            }
            return kept;
        }

        // Build/clean cycles: objects written by parallel compilers, deleted, rewritten; a log grows.
        std::vector<FileSpec> devBuilds(Generator& gen, const std::wstring& repo, std::uint64_t bytes) {
            const std::wstring log = repo + L"\\build\\build.log";
            std::vector<FileSpec> kept = gen.write({{log, 4 * KB, gen.nextSeed()}}, {});
            std::vector<FileSpec> objects;
            constexpr int kCycles = 3;
            for (int cycle = 0; cycle < kCycles; ++cycle) {
                for (const FileSpec& old : objects) {
                    gen.remove(old);
                }
                const Sizing s = scaled({120.0 * KB, 1.0, KB, 16 * MB}, bytes, 8);
                objects = gen.write(makeTree(gen, repo + L"\\build\\obj", bytes, s, {kObjectExt, 25}), {4, 64 * KB});
                gen.append(log, bytes / 200);
            }
            addAll(kept, objects);
            return kept;
        }

        // One repository: sources and history, dependencies, build output.
        std::vector<FileSpec> devRepo(Generator& gen, const std::wstring& repo, std::uint64_t bytes) {
            std::vector<FileSpec> kept = devSources(gen, repo, bytes * 15 / 100, bytes * 25 / 100);
            addAll(kept,
                   gen.write(makeTree(gen, repo + L"\\node_modules", bytes * 15 / 100, {3.0 * KB, 1.5, 50, 2 * MB},
                                      {kModuleExt, 50, Content::Text}),
                             {1, 64 * KB}));
            addAll(kept, devBuilds(gen, repo, bytes * 40 / 100));
            return kept;
        }

        // Development storage (plan.md 8.3): repositories with history, node_modules and build churn.
        std::vector<FileSpec> genDev(Generator& gen, const std::wstring& dir, std::uint64_t bytes) {
            const auto repos = 2 + gen.rng().below(3);
            std::vector<FileSpec> kept;
            for (std::uint64_t r = 0; r < repos; ++r) {
                addAll(kept, devRepo(gen, std::format(L"{}\\repo-{}", dir, r), bytes / repos));
            }
            return kept;
        }

        // --- archive ---------------------------------------------------------------------------------

        constexpr std::array<std::wstring_view, 6> kArchiveExt{L"iso", L"zip", L"7z", L"vhdx", L"bin", L"dat"};

        // Archive, disk image and unknown-file storage (plan.md 8.4): big files copied two or three at a
        // time, a quarter deleted, then more copied into the holes.
        std::vector<FileSpec> genArchive(Generator& gen, const std::wstring& dir, std::uint64_t bytes) {
            const Sizing s = scaled({300.0 * MB, 1.0, 10 * MB, 4700 * MB}, bytes, 6);
            auto kept = gen.write(makeFlat(gen, dir + L"\\set-1", bytes / 2, s, kArchiveExt), {2, 8 * MB});
            kept = gen.removeSome(kept, 0.25);
            addAll(kept, gen.write(makeFlat(gen, dir + L"\\set-2", bytes / 2, s, kArchiveExt), {3, 4 * MB}));
            return kept;
        }

        // --- photos ----------------------------------------------------------------------------------

        // One day's import: photos copied four at a time, some with sidecars, the catalog updated after.
        std::vector<FileSpec> photoDay(Generator& gen, const std::wstring& dayDir, std::uint64_t bytes) {
            std::vector<FileSpec> specs;
            const auto shots = 20 + gen.rng().below(130);
            std::uint64_t total = 0;
            for (std::uint64_t n = 0; n < shots && total < bytes; ++n) {
                const std::uint64_t size =
                    std::min(logNormalSize(gen.rng(), 8.0 * MB, 0.5, 2 * MB, 20 * MB), bytes - total);
                specs.push_back({std::format(L"{}\\IMG_{:04}.jpg", dayDir, n), size, gen.nextSeed()});
                total += size;
                if (gen.rng().unit() < 0.3) {
                    specs.push_back({std::format(L"{}\\IMG_{:04}.xmp", dayDir, n), 4 * KB + gen.rng().below(8 * KB),
                                     gen.nextSeed(), Content::Text});
                }
            }
            return gen.write(specs, {4, MB});
        }

        // Photo storage (plan.md 8.5): dated folders, imports, culling and a catalog that keeps growing.
        std::vector<FileSpec> genPhotos(Generator& gen, const std::wstring& dir, std::uint64_t bytes) {
            const std::wstring catalog = dir + L"\\catalog.lrcat";
            std::vector<FileSpec> kept = gen.write({{catalog, MB, gen.nextSeed()}}, {});
            std::uint64_t used = MB;
            for (int day = 0; used < bytes; ++day) {
                const int year = 2018 + day / 40;
                const auto dayDir =
                    std::format(L"{}\\{}\\{}-{:02}-{:02}", dir, year, year, 1 + day % 40 / 4, 1 + day % 4 * 7);
                auto shots = photoDay(gen, dayDir, bytes - used);
                if (shots.empty()) {
                    break;
                }
                if (gen.rng().unit() < 0.3) {
                    shots = gen.removeSome(shots, 0.1);
                }
                for (const FileSpec& f : shots) {
                    used += f.size;
                }
                addAll(kept, shots);
                gen.append(catalog, 256 * KB);
                used += 256 * KB;
            }
            return kept;
        }

        // --- video -----------------------------------------------------------------------------------

        constexpr std::array<std::wstring_view, 4> kVideoExt{L"mkv", L"mp4", L"mov", L"ts"};

        // Video storage (plan.md 8.6): big files recorded or copied in parallel so they interleave.
        std::vector<FileSpec> genVideo(Generator& gen, const std::wstring& dir, std::uint64_t bytes) {
            const Sizing s = scaled({1.5 * GB, 0.8, 100 * MB, 8 * GB}, bytes, 5);
            auto kept = gen.write(makeFlat(gen, dir + L"\\recorded", bytes * 6 / 10, s, kVideoExt), {3, 16 * MB});
            kept = gen.removeSome(kept, 0.3);
            addAll(kept, gen.write(makeFlat(gen, dir + L"\\copied", bytes * 4 / 10, s, kVideoExt), {2, 32 * MB}));
            return kept;
        }

        // --- mixed and derived -----------------------------------------------------------------------

        // Mixed data drive (plan.md 8.7): a bit of every kind.
        std::vector<FileSpec> genMixed(Generator& gen, const std::wstring& dir, std::uint64_t bytes) {
            std::vector<FileSpec> kept = genDev(gen, dir + L"\\dev", bytes * 20 / 100);
            addAll(kept, genPhotos(gen, dir + L"\\photos", bytes * 25 / 100));
            addAll(kept, genArchive(gen, dir + L"\\archive", bytes * 25 / 100));
            addAll(kept, genVideo(gen, dir + L"\\video", bytes * 30 / 100));
            return kept;
        }

        constexpr std::array<std::wstring_view, 4> kFillExt{L"dat", L"bin", L"zip", L"jpg"};

        // Nearly full volume: mixed content, then medium files written three at a time until the
        // budget (97% used by default) is gone.
        std::vector<FileSpec> genAlmostFull(Generator& gen, const std::wstring& dir, std::uint64_t bytes) {
            // Thousands of small files leave cluster slack and NTFS metadata (about 3% of a 4 GB volume) that the
            // byte budget does not see, so the fill phase measures the volume. Metadata varies by a few MB from
            // run to run; the budget is rounded down to 16 MB so the same files are written every time.
            constexpr std::uint64_t kStep = 16 * MB;
            const std::uint64_t target = gen.usedBytes() + bytes;
            std::vector<FileSpec> kept = genMixed(gen, dir + L"\\mixed", bytes * 75 / 100);
            const std::uint64_t used = gen.usedBytes();
            gen.setBudget(target > used ? (target - used) / kStep * kStep : 0);
            const Sizing s = scaled({8.0 * MB, 1.0, MB, 64 * MB}, bytes, 20);
            addAll(kept, gen.write(makeTree(gen, dir + L"\\fill", gen.budget(), s, {kFillExt, 40}), {3, MB}));
            return kept;
        }

        // Creates a shadow copy of the volume through WMI (vssadmin cannot create them on client Windows).
        void createShadowCopy(const Generator& gen) {
            const std::wstring cmd =
                L"powershell.exe -NoProfile -Command \"$r = Invoke-CimMethod -ClassName Win32_ShadowCopy "
                L"-MethodName Create -Arguments @{Volume='" +
                gen.root() + L"'; Context='ClientAccessible'}; exit $r.ReturnValue\"";
            const unsigned long code = runProcess(cmd);
            if (code != 0) {
                throw std::runtime_error(std::format("Win32_ShadowCopy.Create failed with {}", code));
            }
        }

        // Volume with a shadow copy (plan.md 9.4): mixed content, a shadow copy, then changes after it
        // so the copy-on-write storage holds real data.
        std::vector<FileSpec> genVss(Generator& gen, const std::wstring& dir, std::uint64_t bytes) {
            auto kept = genMixed(gen, dir + L"\\mixed", bytes * 80 / 100);
            createShadowCopy(gen);
            kept = gen.removeSome(kept, 0.1);
            for (std::size_t i = 0; i < kept.size(); i += 20) {
                gen.append(kept[i].path, 64 * KB);
            }
            const Sizing s = scaled({50.0 * MB, 1.0, MB, 512 * MB}, bytes, 10);
            addAll(kept, gen.write(makeFlat(gen, dir + L"\\after", gen.budget(), s, kArchiveExt), {2, 4 * MB}));
            return kept;
        }

        // --- special ---------------------------------------------------------------------------------

        // Tiny files that NTFS keeps inside their MFT record (no clusters), plus empty files.
        std::vector<FileSpec> specialResident(Generator& gen, const std::wstring& dir) {
            std::vector<FileSpec> specs;
            for (int n = 0; n < 200; ++n) {
                specs.push_back({std::format(L"{}\\resident\\r{}.txt", dir, n), gen.rng().below(600), gen.nextSeed(),
                                 Content::Text});
            }
            specs.push_back({dir + L"\\resident\\empty.txt", 0, gen.nextSeed()});
            return gen.write(specs, {});
        }

        // A compressed directory with compressible and incompressible files written interleaved.
        std::vector<FileSpec> specialCompressed(Generator& gen, const std::wstring& dir) {
            gen.makeDirs(dir + L"\\compressed");
            setCompressed(gen, dir + L"\\compressed");
            std::vector<FileSpec> specs;
            for (int n = 0; n < 3; ++n) {
                specs.push_back({std::format(L"{}\\compressed\\text-{}.log", dir, n), (8 + 8 * n) * MB, gen.nextSeed(),
                                 Content::Text});
            }
            specs.push_back({dir + L"\\compressed\\random.bin", 4 * MB, gen.nextSeed()});
            return gen.write(specs, {4, 64 * KB});
        }

        // Hard links, symbolic links and a junction.
        std::vector<FileSpec> specialLinks(Generator& gen, const std::wstring& dir) {
            auto kept = gen.write({{dir + L"\\links\\target.bin", 2 * MB, gen.nextSeed()}}, {});
            hardLink(gen, dir + L"\\links\\hard-1.bin", dir + L"\\links\\target.bin");
            gen.makeDirs(dir + L"\\links\\other");
            hardLink(gen, dir + L"\\links\\other\\hard-2.bin", dir + L"\\links\\target.bin");
            symbolicLink(gen, dir + L"\\links\\file-symlink.bin", dir + L"\\links\\target.bin", false);
            symbolicLink(gen, dir + L"\\links\\dir-symlink", dir + L"\\compressed", true);
            junction(gen, dir + L"\\links\\junction", dir + L"\\resident");
            return kept;
        }

        // Alternate data streams: a big fragmented one and a resident one.
        std::vector<FileSpec> specialStreams(Generator& gen, const std::wstring& dir) {
            auto kept = gen.write({{dir + L"\\streams\\host.txt", 300, gen.nextSeed(), Content::Text}}, {});
            const auto written = gen.write({{dir + L"\\streams\\host.txt:big", 3 * MB, gen.nextSeed()},
                                            {dir + L"\\streams\\neighbour.bin", 3 * MB, gen.nextSeed()},
                                            {dir + L"\\streams\\host.txt:tag", 40, gen.nextSeed(), Content::Text}},
                                           {2, 64 * KB});
            // Streams are not files of their own: later steps must not delete or append to them by name.
            std::ranges::copy_if(written, std::back_inserter(kept),
                                 [](const FileSpec& f) { return !f.path.contains(L':'); });
            return kept;
        }

        // A file in thousands of pieces, so its extents spill into an $ATTRIBUTE_LIST.
        std::vector<FileSpec> specialManyFragments(Generator& gen, const std::wstring& dir) {
            auto both = gen.write({{dir + L"\\fragmented\\many.bin", 96 * MB, gen.nextSeed()},
                                   {dir + L"\\fragmented\\filler.bin", 96 * MB, gen.nextSeed()}},
                                  {2, 4 * KB});
            const auto filler =
                std::ranges::find_if(both, [](const FileSpec& f) { return f.path.ends_with(L"filler.bin"); });
            gen.remove(*filler);
            both.erase(filler);
            return both;
        }

        // Odd names and attributes: unicode, a path over 260 characters, read-only, hidden, system, EFS.
        std::vector<FileSpec> specialOdd(Generator& gen, const std::wstring& dir) {
            const std::wstring longDir = dir + L"\\long\\" + std::wstring(120, L'a') + L"\\" + std::wstring(120, L'b');
            auto kept = gen.write({{dir + L"\\odd\\фото-日本.txt", 64 * KB, gen.nextSeed()},
                                   {longDir + L"\\" + std::wstring(60, L'c') + L".bin", 256 * KB, gen.nextSeed()},
                                   {dir + L"\\odd\\readonly.bin", 128 * KB, gen.nextSeed()},
                                   {dir + L"\\odd\\hidden-system.bin", 128 * KB, gen.nextSeed()},
                                   {dir + L"\\efs\\secret.bin", MB, gen.nextSeed()}},
                                  {});
            setAttributes(gen, dir + L"\\odd\\readonly.bin", FILE_ATTRIBUTE_READONLY);
            setAttributes(gen, dir + L"\\odd\\hidden-system.bin", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
            encrypt(gen, dir + L"\\efs\\secret.bin");
            return kept;
        }

        // Special cases the scanner must flag (plan.md 10.2): resident, compressed, sparse, hard links,
        // reparse points, alternate streams, encrypted, many fragments, a big directory.
        std::vector<FileSpec> genSpecial(Generator& gen, const std::wstring& dir, std::uint64_t /*bytes*/) {
            std::vector<FileSpec> kept = specialResident(gen, dir);
            addAll(kept, specialCompressed(gen, dir));
            writeSparse(gen, dir + L"\\sparse.bin", GB, 40, 64 * KB);
            addAll(kept, specialLinks(gen, dir));
            addAll(kept, specialStreams(gen, dir));
            addAll(kept, specialManyFragments(gen, dir));
            addAll(kept, specialOdd(gen, dir));
            std::vector<FileSpec> big;
            for (int n = 0; n < 3000; ++n) {
                big.push_back(
                    {std::format(L"{}\\bigdir\\entry-{:05}.txt", dir, n), 200, gen.nextSeed(), Content::Text});
            }
            addAll(kept, gen.write(big, {}));
            return kept;
        }

        constexpr std::array<Scenario, 8> kScenarios{{
            {L"dev", 0.5, genDev, "repositories, node_modules, build/clean cycles, growing packs and logs"},
            {L"archive", 0.6, genArchive, "ISOs, zips, VHDXs and blobs copied in parallel, some deleted"},
            {L"photos", 0.6, genPhotos, "dated photo folders with sidecars, culling, a growing catalog"},
            {L"video", 0.7, genVideo, "100 MB-8 GB files written in parallel so they interleave"},
            {L"mixed", 0.6, genMixed, "dev, photos, archive and video together"},
            {L"almost-full", 0.97, genAlmostFull, "mixed, then filled to 97% used"},
            {L"vss", 0.5, genVss, "mixed, a shadow copy, then changes after it"},
            {L"special", 0.0, genSpecial, "compressed, sparse, links, junction, ADS, EFS, resident, many fragments"},
        }};

    } // namespace

    std::span<const Scenario> scenarios() { return kScenarios; }

    const Scenario* findScenario(std::wstring_view name) {
        const auto it = std::ranges::find(kScenarios, name, &Scenario::name);
        return it == kScenarios.end() ? nullptr : &*it;
    }

} // namespace icd::testvol
