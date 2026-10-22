// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vfs.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <unordered_map>

#include <vpkpp/format/VPK.h>

#include "../kv1/Kv1.h"

namespace xv::vfs {

namespace fs = std::filesystem;

namespace {

std::string lowerAscii(std::string_view text) {
	std::string out{text};
	std::ranges::transform(out, out.begin(), [](char c) {
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	});
	return out;
}

std::vector<std::string> splitIds(std::string_view keys) {
	std::vector<std::string> ids;
	std::size_t start = 0;
	while (start <= keys.size()) {
		const auto end = std::min(keys.find('+', start), keys.size());
		if (end > start) {
			ids.push_back(lowerAscii(keys.substr(start, end - start)));
		}
		start = end + 1;
	}
	return ids;
}

// Handles [$X] and [!$X]; anything more elaborate is assumed to apply.
bool conditionHolds(std::optional<std::string_view> condition) {
	if (!condition) {
		return true;
	}
	std::string_view c = *condition;
	if (c.starts_with('[')) {
		c.remove_prefix(1);
	}
	if (c.ends_with(']')) {
		c.remove_suffix(1);
	}
	const bool negate = c.starts_with('!');
	if (negate) {
		c.remove_prefix(1);
	}
	if (!c.starts_with('$') || c.find_first_of(" |&") != std::string_view::npos) {
		return true;
	}
	c.remove_prefix(1);
#if defined(_WIN32)
	const bool holds = c == "WIN32" || c == "WIN64" || c == "WINDOWS";
#elif defined(__APPLE__)
	const bool holds = c == "OSX" || c == "POSIX";
#else
	const bool holds = c == "LINUX" || c == "POSIX";
#endif
	return negate != holds;
}

// Directory entries, or none if the folder can't be read; never throws.
std::vector<fs::directory_entry> entriesOf(const fs::path& directory) {
	std::vector<fs::directory_entry> entries;
	std::error_code ec;
	for (fs::directory_iterator it{directory, ec}; !ec && it != fs::directory_iterator{}; it.increment(ec)) {
		entries.push_back(*it);
	}
	return entries;
}

bool isVpkArchivePart(const fs::path& file) {
	// name_000.vpk, name_001.vpk, ... hold data for name_dir.vpk and are not mounted on their own.
	const std::string stem = toUtf8(file.stem());
	return stem.size() > 4 && stem[stem.size() - 4] == '_'
		&& std::all_of(stem.end() - 3, stem.end(), [](char c) { return c >= '0' && c <= '9'; });
}

class DirectoryMount final : public Mount {
public:
	DirectoryMount(const fs::path& location, std::vector<std::string> ids)
			: Mount(Kind::Directory, location, std::move(ids)) {
		std::error_code ec;
		auto it = fs::recursive_directory_iterator(location, fs::directory_options::skip_permission_denied, ec);
		for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
			std::error_code typeError;
			if (it->is_regular_file(typeError)) {
				files_.emplace(normalizePath(toUtf8(it->path().lexically_relative(location))), it->path());
			}
		}
	}

	[[nodiscard]] bool contains(const std::string& path) const override {
		return files_.contains(path);
	}

	[[nodiscard]] std::optional<std::vector<std::byte>> read(const std::string& path) const override {
		const auto it = files_.find(path);
		if (it == files_.end()) {
			return std::nullopt;
		}
		std::ifstream stream{it->second, std::ios::binary};
		if (!stream) {
			return std::nullopt;
		}
		std::vector<char> chars{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
		std::vector<std::byte> bytes(chars.size());
		std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
		return bytes;
	}

	void forEachFile(const std::function<void(const std::string&)>& callback) const override {
		for (const auto& [path, _] : files_) {
			callback(path);
		}
	}

	[[nodiscard]] std::size_t fileCount() const override {
		return files_.size();
	}

	[[nodiscard]] std::optional<fs::path> diskPath(const std::string& path) const override {
		const auto it = files_.find(path);
		return it == files_.end() ? std::nullopt : std::optional{it->second};
	}

private:
	std::unordered_map<std::string, fs::path> files_; // normalized path -> real file
};

class VpkMount final : public Mount {
public:
	VpkMount(const fs::path& location, std::vector<std::string> ids, std::unique_ptr<vpkpp::PackFile> pack)
			: Mount(Kind::Vpk, location, std::move(ids))
			, pack_(std::move(pack)) {
		pack_->runForAllEntries([this](const std::string& path, const vpkpp::Entry&) {
			entries_.emplace(normalizePath(path), path);
		});
	}

	[[nodiscard]] bool contains(const std::string& path) const override {
		return entries_.contains(path);
	}

	[[nodiscard]] std::optional<std::vector<std::byte>> read(const std::string& path) const override {
		const auto it = entries_.find(path);
		if (it == entries_.end()) {
			return std::nullopt;
		}
		return pack_->readEntry(it->second);
	}

	void forEachFile(const std::function<void(const std::string&)>& callback) const override {
		for (const auto& [path, _] : entries_) {
			callback(path);
		}
	}

	[[nodiscard]] std::size_t fileCount() const override {
		return entries_.size();
	}

private:
	std::unique_ptr<vpkpp::PackFile> pack_;
	std::unordered_map<std::string, std::string> entries_; // normalized path -> path inside the VPK
};

} // namespace

std::string toUtf8(const fs::path& path) {
	const std::u8string utf8 = path.u8string();
	return {utf8.begin(), utf8.end()};
}

fs::path fromUtf8(std::string_view utf8) {
	return fs::path{std::u8string{utf8.begin(), utf8.end()}};
}

std::optional<fs::path> Mount::diskPath(const std::string&) const {
	return std::nullopt;
}

std::string normalizePath(std::string_view path) {
	std::string out;
	out.reserve(path.size());
	for (const char c : path) {
		const char slash = c == '\\' ? '/' : c;
		if (slash == '/' && (out.empty() || out.back() == '/')) {
			continue;
		}
		out += slash;
	}
	return lowerAscii(out);
}

fs::path findCaseInsensitive(const fs::path& path) {
	std::error_code ec;
	if (fs::exists(path, ec)) {
		return path;
	}
	fs::path current = path.root_path();
	for (const fs::path& component : path.relative_path()) {
		if (fs::exists(current / component, ec)) {
			current /= component;
			continue;
		}
		const std::string wanted = lowerAscii(toUtf8(component));
		bool found = false;
		for (const auto& entry : entriesOf(current)) {
			if (lowerAscii(toUtf8(entry.path().filename())) == wanted) {
				current = entry.path();
				found = true;
				break;
			}
		}
		if (!found) {
			return path;
		}
	}
	return current;
}

std::optional<GameInfo> GameInfo::load(const fs::path& file, std::string* error, fs::path baseDir) {
	std::ifstream stream{file, std::ios::binary};
	if (!stream) {
		if (error) {
			*error = "cannot open " + toUtf8(file);
		}
		return std::nullopt;
	}
	const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	return parse(text, file, error, std::move(baseDir));
}

std::optional<GameInfo> GameInfo::parse(std::string_view text, const fs::path& file, std::string* error, fs::path baseDir) {
	const auto fail = [error](std::string message) -> std::optional<GameInfo> {
		if (error) {
			*error = std::move(message);
		}
		return std::nullopt;
	};

	const auto parsed = kv1::parse(text);
	const kv1::Node* root = parsed.document.find("GameInfo");
	if (!root || !root->isBlock()) {
		return fail("no GameInfo block");
	}
	const kv1::Node* fileSystem = root->find("FileSystem");
	const kv1::Node* searchPaths = fileSystem ? fileSystem->find("SearchPaths") : nullptr;
	if (!searchPaths || !searchPaths->isBlock()) {
		return fail("no FileSystem/SearchPaths block");
	}

	GameInfo info;
	info.file = fs::absolute(file);
	info.gameDir = info.file.parent_path();
	info.baseDir = baseDir.empty() ? info.gameDir.parent_path() : fs::absolute(baseDir);
	if (const kv1::Node* game = root->find("game")) {
		info.game = game->value();
	}
	if (const kv1::Node* title = root->find("title")) {
		info.title = title->value();
	}
	for (const kv1::Node& entry : searchPaths->children()) {
		if (entry.isPair() && conditionHolds(entry.condition())) {
			info.searchPaths.push_back({splitIds(entry.key()), entry.value()});
		}
	}
	return info;
}

Mount::Mount(Kind kind, fs::path location, std::vector<std::string> ids)
		: kind_(kind)
		, location_(std::move(location))
		, ids_(std::move(ids)) {}

bool Mount::hasId(std::string_view id) const {
	return std::ranges::find(ids_, lowerAscii(id)) != ids_.end();
}

void FileSystem::clear() {
	records_.clear();
	mounts_.clear();
}

void FileSystem::mount(const GameInfo& info) {
	clear();
	for (const SearchPath& searchPath : info.searchPaths) {
		mountSearchPath(info, searchPath);
	}
}

const Mount* FileSystem::addDirectory(const fs::path& directory, std::vector<std::string> ids, std::string* error) {
	std::error_code ec;
	if (!fs::is_directory(directory, ec)) {
		if (error) {
			*error = "not a directory: " + toUtf8(directory);
		}
		return nullptr;
	}
	auto& mount = mounts_.emplace_back(std::make_unique<DirectoryMount>(directory, ids));
	records_.push_back({toUtf8(directory), directory, std::move(ids), MountRecord::Status::Mounted, {}, mount.get()});
	return mount.get();
}

const Mount* FileSystem::addVpk(const fs::path& file, std::vector<std::string> ids, std::string* error) {
	// sourcepp takes paths as std::string, which std::filesystem reads in the process code page.
	// The Windows build declares that code page to be UTF-8 in its manifest, so this is lossless there too.
	auto pack = vpkpp::VPK::open(toUtf8(file));
	if (!pack) {
		if (error) {
			*error = "could not open VPK: " + toUtf8(file);
		}
		return nullptr;
	}
	auto& mount = mounts_.emplace_back(std::make_unique<VpkMount>(file, ids, std::move(pack)));
	records_.push_back({toUtf8(file), file, std::move(ids), MountRecord::Status::Mounted, {}, mount.get()});
	return mount.get();
}

void FileSystem::record(const SearchPath& searchPath, const fs::path& location, MountRecord::Status status, std::string message, const Mount* mount) {
	if (mount) {
		// addDirectory/addVpk already recorded it; attribute it to the gameinfo entry.
		records_.back().searchPath = searchPath.path;
		return;
	}
	records_.push_back({searchPath.path, location, searchPath.ids, status, std::move(message), nullptr});
}

void FileSystem::mountSearchPath(const GameInfo& info, const SearchPath& searchPath) {
	std::string expanded = searchPath.path;
	const auto substitute = [&expanded](std::string_view token, const fs::path& value) {
		for (auto pos = expanded.find(token); pos != std::string::npos; pos = expanded.find(token)) {
			expanded.replace(pos, token.size(), toUtf8(value) + "/");
		}
	};
	substitute("|gameinfo_path|", info.gameDir);
	substitute("|all_source_engine_paths|", info.baseDir);

	fs::path path = fromUtf8(expanded);
	if (path.is_relative()) {
		path = info.baseDir / path;
	}
	path = path.lexically_normal();
	std::error_code ec;

	// "custom/*": every folder and VPK inside, alphabetically.
	if (path.filename() == "*") {
		const fs::path directory = findCaseInsensitive(path.parent_path());
		if (!fs::is_directory(directory, ec)) {
			record(searchPath, directory, MountRecord::Status::Missing, "folder not found", nullptr);
			return;
		}
		std::vector<fs::directory_entry> entries = entriesOf(directory);
		std::ranges::sort(entries, {}, [](const fs::directory_entry& e) { return lowerAscii(toUtf8(e.path().filename())); });
		for (const auto& entry : entries) {
			std::string error;
			const Mount* mount = nullptr;
			if (entry.is_directory(ec)) {
				mount = addDirectory(entry.path(), searchPath.ids, &error);
			} else if (lowerAscii(toUtf8(entry.path().extension())) == ".vpk" && !isVpkArchivePart(entry.path())) {
				mount = addVpk(entry.path(), searchPath.ids, &error);
			} else {
				continue;
			}
			record(searchPath, entry.path(), MountRecord::Status::Failed, std::move(error), mount);
		}
		return;
	}

	// "cstrike/cstrike_pak.vpk" means cstrike_pak_dir.vpk when the VPK is split into archives.
	if (lowerAscii(toUtf8(path.extension())) == ".vpk") {
		const fs::path split = findCaseInsensitive(path.parent_path() / fromUtf8(toUtf8(path.stem()) + "_dir.vpk"));
		const fs::path single = findCaseInsensitive(path);
		const fs::path file = fs::exists(split, ec) ? split : single;
		if (!fs::exists(file, ec)) {
			record(searchPath, path, MountRecord::Status::Missing, "VPK not found", nullptr);
			return;
		}
		std::string error;
		const Mount* mount = addVpk(file, searchPath.ids, &error);
		record(searchPath, file, MountRecord::Status::Failed, std::move(error), mount);
		return;
	}

	const fs::path directory = findCaseInsensitive(path);
	if (!fs::is_directory(directory, ec)) {
		record(searchPath, directory, MountRecord::Status::Missing, "folder not found", nullptr);
		return;
	}
	std::string error;
	const Mount* mount = addDirectory(directory, searchPath.ids, &error);
	record(searchPath, directory, MountRecord::Status::Failed, std::move(error), mount);
}

const Mount* FileSystem::resolve(std::string_view path, std::string_view pathId) const {
	const std::string normalized = normalizePath(path);
	for (const auto& mount : mounts_) {
		if (mount->hasId(pathId) && mount->contains(normalized)) {
			return mount.get();
		}
	}
	return nullptr;
}

std::optional<std::vector<std::byte>> FileSystem::read(std::string_view path, std::string_view pathId) const {
	const Mount* mount = resolve(path, pathId);
	return mount ? mount->read(normalizePath(path)) : std::nullopt;
}

std::optional<std::string> FileSystem::readText(std::string_view path, std::string_view pathId) const {
	const auto bytes = read(path, pathId);
	if (!bytes) {
		return std::nullopt;
	}
	return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

std::map<std::string, const Mount*> FileSystem::files(std::string_view pathId) const {
	std::map<std::string, const Mount*> out;
	for (const auto& mount : mounts_) {
		if (mount->hasId(pathId)) {
			mount->forEachFile([&out, m = mount.get()](const std::string& path) { out.emplace(path, m); });
		}
	}
	return out;
}

std::vector<std::string> FileSystem::find(std::string_view directory, std::string_view extension, bool recursive, std::string_view pathId) const {
	std::string prefix = normalizePath(directory);
	if (!prefix.empty() && !prefix.ends_with('/')) {
		prefix += '/';
	}
	const std::string suffix = lowerAscii(extension);
	std::set<std::string> found;
	for (const auto& mount : mounts_) {
		if (!mount->hasId(pathId)) {
			continue;
		}
		mount->forEachFile([&](const std::string& path) {
			if (path.starts_with(prefix) && path.ends_with(suffix)
				&& (recursive || path.find('/', prefix.size()) == std::string::npos)) {
				found.insert(path);
			}
		});
	}
	return {found.begin(), found.end()};
}

} // namespace xv::vfs
