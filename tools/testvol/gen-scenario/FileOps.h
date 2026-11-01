#pragma once

// File operations for the scenario generator: deterministic content, interleaved writers that make NTFS
// fragment files the way parallel copies, appends and deletes do on real drives, and the special NTFS
// features (compression, sparse files, links, streams, encryption) the scanner must recognise.

#include "core/synth/SynthRandom.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace icd::testvol {

    // What a file's bytes look like; only matters for compressed files.
    enum class Content : std::uint8_t {
        Random, // Incompressible, like media and archives.
        Text, // Repetitive words, so NTFS compression really shrinks it.
    };

    // One file to write: path relative to the volume root, final size and the seed of its bytes.
    struct FileSpec {
        std::wstring path; // Relative path, e.g. L"photos\\2021\\img-1.jpg".
        std::uint64_t size = 0; // Final size in bytes.
        std::uint64_t seed = 0; // Content seed: same seed, same bytes.
        Content content = Content::Random; // Byte pattern.
    };

    // How a batch of files is written: how many grow at the same time and how much each append adds.
    // More streams and smaller chunks interleave their clusters more, so the files fragment more.
    struct WriteMode {
        int streams = 1; // Files open and growing at once.
        std::uint64_t chunk = 1ULL << 20U; // Bytes per append.
    };

    // Totals reported at the end, so a run shows what it produced.
    struct Totals {
        std::uint64_t filesWritten = 0; // Files created (deleted ones included).
        std::uint64_t filesDeleted = 0; // Files removed again to leave holes.
        std::uint64_t bytesWritten = 0; // All bytes written, appends included.
    };

    // Shared state of one generator run: the volume root, the seeded random source and the byte budget.
    class Generator {
    public:
        // root is like L"T:\\"; budget is how many bytes the scenario may leave on the volume.
        Generator(std::wstring root, std::uint64_t seed, std::uint64_t budget);

        // Random source for scenario decisions (sizes, names, order); content has its own seeds.
        core::SynthRandom& rng() { return rng_; }
        // Bytes still allowed on the volume by this run.
        std::uint64_t budget() const { return budget_; }
        // Totals so far.
        const Totals& totals() const { return totals_; }
        // Bytes in use on the volume right now (files, slack in their last clusters, NTFS metadata).
        std::uint64_t usedBytes() const;
        // Replaces the remaining budget (scenarios that correct it for cluster slack and NTFS metadata).
        void setBudget(std::uint64_t bytes) { budget_ = bytes; }
        // A fresh content seed, so every file and append gets its own bytes.
        std::uint64_t nextSeed() { return ++seedCounter_; }
        // Volume root as given (L"T:\\").
        const std::wstring& root() const { return root_; }

        // "\\?\T:\rel" so long paths work.
        std::wstring full(const std::wstring& rel) const;
        // Creates a directory and its parents (remembered, so repeated calls are free).
        void makeDirs(const std::wstring& relDir);

        // Writes the files, `mode.streams` at a time in random append order; sizes are clamped to the
        // budget and files that no longer fit are dropped. Returns the specs actually written.
        std::vector<FileSpec> write(std::vector<FileSpec> specs, WriteMode mode);
        // Appends bytes to an existing file in one go (logs, packs growing later).
        void append(const std::wstring& rel, std::uint64_t bytes);
        // Deletes about `share` of the files (chosen at random) and returns the ones kept.
        std::vector<FileSpec> removeSome(std::vector<FileSpec> files, double share);
        // Deletes one file and gives its bytes back to the budget.
        void remove(const FileSpec& file);

    private:
        // Takes up to `want` bytes from the budget; returns what was granted.
        std::uint64_t take(std::uint64_t want);

        std::wstring root_; // Volume root, e.g. L"T:\\".
        core::SynthRandom rng_; // Scenario decisions.
        std::uint64_t budget_ = 0; // Bytes still allowed.
        std::uint64_t seedCounter_ = 0; // Last content seed handed out.
        Totals totals_; // What was done.
        std::unordered_set<std::wstring> dirs_; // Directories already created.
    };

    // Log-normal size around `median`, clamped to [lo, hi]; real file sizes look like this.
    std::uint64_t logNormalSize(core::SynthRandom& rng, double median, double sigma, std::uint64_t lo,
                                std::uint64_t hi);

    // Special NTFS features, each on a path relative to the root.

    // Marks a directory compressed (FSCTL_SET_COMPRESSION), so files created in it later are stored
    // compressed while they are written.
    void setCompressed(const Generator& gen, const std::wstring& relDir);
    // Creates a sparse file of `size` bytes with `blocks` random blocks of data in it.
    void writeSparse(Generator& gen, const std::wstring& rel, std::uint64_t size, int blocks, std::uint64_t blockBytes);
    // Adds another name for an existing file.
    void hardLink(const Generator& gen, const std::wstring& link, const std::wstring& target);
    // Creates a file or directory symbolic link.
    void symbolicLink(const Generator& gen, const std::wstring& link, const std::wstring& target, bool directory);
    // Creates a directory junction (mklink /J).
    void junction(const Generator& gen, const std::wstring& link, const std::wstring& target);
    // Encrypts a file with EFS.
    void encrypt(const Generator& gen, const std::wstring& rel);
    // Sets file attributes (read-only, hidden, system).
    void setAttributes(const Generator& gen, const std::wstring& rel, unsigned long attributes);

    // Fragmentation found on the volume afterwards, so a run proves it left some.
    struct FragmentStats {
        std::uint64_t files = 0; // Files with clusters (resident ones excluded).
        std::uint64_t fragmented = 0; // Files in more than one piece.
        std::uint64_t fragments = 0; // Pieces over all files.
        std::uint64_t maxFragments = 0; // Most pieces of one file.
    };
    // Walks the tree under rel and counts each file's extents (FSCTL_GET_RETRIEVAL_POINTERS), joining
    // runs that continue on disk; reparse points are not followed.
    FragmentStats countFragments(const Generator& gen, const std::wstring& rel);

    // Runs a command line and waits; returns its exit code (shadow copy creation, mklink).
    unsigned long runProcess(const std::wstring& commandLine);

} // namespace icd::testvol
