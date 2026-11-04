// Folder walk (T076c): every file and folder below a folder once, parents before children, junctions listed but not
// entered, a missing folder refused.

#include "platform/win/FolderWalk.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

namespace icd::platform { namespace {

    namespace fs = std::filesystem;

    // A fresh folder under the temp directory, removed afterwards.
    class TempTree {
    public:
        TempTree() : root_(fs::temp_directory_path() / "icd-folder-walk-test") {
            fs::remove_all(root_);
            fs::create_directories(root_ / "proj" / "src");
            fs::create_directories(root_ / "proj" / "target" / "debug");
            fs::create_directories(root_ / "empty");
            write(root_ / "proj" / "Cargo.toml", "[package]");
            write(root_ / "proj" / "src" / "main.rs", "fn main() {}");
            write(root_ / "proj" / "target" / "debug" / "app.exe", std::string(5000, 'x'));
            write(root_ / "top.txt", "");
        }
        ~TempTree() {
            std::error_code ignored;
            fs::remove_all(root_, ignored);
        }
        TempTree(const TempTree&) = delete;
        TempTree& operator=(const TempTree&) = delete;

        // The tree's root.
        [[nodiscard]] const fs::path& root() const { return root_; }

    private:
        // Writes a small file.
        static void write(const fs::path& path, const std::string& text) { std::ofstream(path) << text; }

        fs::path root_; // The tree's root.
    };

    // The path of an entry relative to the walked folder.
    std::string pathOf(const FolderWalk& w, std::size_t i) {
        const FolderEntry& e = w.entries[i];
        return e.parent == walkRoot ? e.name : pathOf(w, e.parent) + "/" + e.name;
    }

    TEST(FolderWalk, ListsEveryEntryOnceParentsFirst) {
        const TempTree tree;
        const FolderWalk w = walkFolder(tree.root().wstring() + L"\\");
        std::map<std::string, const FolderEntry*> byPath;
        for (std::size_t i = 0; i < w.entries.size(); ++i) {
            const std::size_t parent = w.entries[i].parent;
            ASSERT_TRUE(parent == walkRoot || parent < i); // A folder before its contents.
            EXPECT_TRUE(byPath.emplace(pathOf(w, i), &w.entries[i]).second) << pathOf(w, i);
        }
        EXPECT_EQ(byPath.size(), 9U);
        EXPECT_EQ(w.unreadable, 0U);
        ASSERT_TRUE(byPath.contains("proj/target/debug/app.exe"));
        EXPECT_EQ(byPath["proj/target/debug/app.exe"]->sizeBytes, 5000U);
        EXPECT_FALSE(byPath["proj/target/debug/app.exe"]->directory);
        EXPECT_GT(byPath["proj/target/debug/app.exe"]->modified, 0);
        EXPECT_TRUE(byPath["empty"]->directory);
        EXPECT_EQ(byPath["empty"]->sizeBytes, 0U);
    }

    TEST(FolderWalk, JunctionsAreListedButNotEntered) {
        const TempTree tree;
        const fs::path link = tree.root() / "loop";
        const std::wstring command =
            L"cmd /c mklink /J \"" + link.wstring() + L"\" \"" + tree.root().wstring() + L"\" >nul 2>&1";
        if (_wsystem(command.c_str()) != 0) {
            GTEST_SKIP() << "cannot create a junction here";
        }
        const FolderWalk w = walkFolder(tree.root().wstring());
        std::size_t loops = 0;
        for (const FolderEntry& e : w.entries) {
            loops += e.name == "loop" ? 1 : 0;
            EXPECT_NE(e.parent != walkRoot ? w.entries[e.parent].name : std::string(), "loop"); // Not entered.
        }
        EXPECT_EQ(loops, 1U);
        fs::remove(link);
    }

    TEST(FolderWalk, MissingFolderIsRefused) {
        EXPECT_THROW((void)walkFolder(L"Z:\\no\\such\\icd\\folder"), std::runtime_error);
    }

}} // namespace icd::platform
