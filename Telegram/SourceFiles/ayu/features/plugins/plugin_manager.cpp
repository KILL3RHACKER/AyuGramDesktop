// This is the source code of AyuGram Mod for Desktop.
#include "ayu/features/plugins/plugin_manager.h"

#include "core/launcher.h"
#include "ayu/ui/boxes/plugin_info_box.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QColor>
#include <map>

extern "C" {
#include "ayu/libs/lua/src/lua.h"
#include "ayu/libs/lua/src/lauxlib.h"
#include "ayu/libs/lua/src/lualib.h"
}

namespace AyuPlugins {
namespace {

static PluginManager *s_manager = nullptr;

int LuaLog(lua_State *L) {
	const auto str = luaL_checkstring(L, 1);
	LOG(("AyuPlugins: %1").arg(QString::fromUtf8(str)));
	return 0;
}

int LuaSetProfileGradient(lua_State *L) {
	const auto userId = static_cast<uint64>(luaL_checkinteger(L, 1));
	const auto col1Str = QString::fromUtf8(luaL_checkstring(L, 2));
	const auto col2Str = QString::fromUtf8(luaL_checkstring(L, 3));

	const auto c1 = QColor(col1Str);
	const auto c2 = QColor(col2Str);
	if (c1.isValid() && c2.isValid() && s_manager) {
		auto style = s_manager->getProfileStyle(userId).value_or(CustomProfileStyle{});
		style.hasGradient = true;
		style.gradientColor1 = c1;
		style.gradientColor2 = c2;
		s_manager->setProfileStyle(userId, style);
	}
	return 0;
}

int LuaSetProfileBadge(lua_State *L) {
	const auto userId = static_cast<uint64>(luaL_checkinteger(L, 1));
	const auto badgeText = QString::fromUtf8(luaL_checkstring(L, 2));

	if (s_manager) {
		auto style = s_manager->getProfileStyle(userId).value_or(CustomProfileStyle{});
		style.hasCustomBadge = true;
		style.badgeText = badgeText;
		s_manager->setProfileStyle(userId, style);
	}
	return 0;
}

int LuaSetAvatarShape(lua_State *L) {
	const auto userId = static_cast<uint64>(luaL_checkinteger(L, 1));
	const auto radius = static_cast<int>(luaL_checkinteger(L, 2));

	if (s_manager) {
		auto style = s_manager->getProfileStyle(userId).value_or(CustomProfileStyle{});
		style.hasAvatarShape = true;
		style.avatarRadius = radius;
		s_manager->setProfileStyle(userId, style);
	}
	return 0;
}

} // namespace

struct PluginManager::Impl {
	lua_State *L = nullptr;
	std::vector<PluginInfo> plugins;
	std::map<uint64, CustomProfileStyle> profileStyles;

	void registerLuaBindings() {
		if (!L) {
			return;
		}
		lua_newtable(L);

		lua_pushcfunction(L, LuaLog);
		lua_setfield(L, -2, "log");

		lua_pushcfunction(L, LuaSetProfileGradient);
		lua_setfield(L, -2, "set_profile_gradient");

		lua_pushcfunction(L, LuaSetProfileBadge);
		lua_setfield(L, -2, "set_profile_badge");

		lua_pushcfunction(L, LuaSetAvatarShape);
		lua_setfield(L, -2, "set_avatar_shape");

		lua_setglobal(L, "telegram");
	}
};

PluginManager &PluginManager::Instance() {
	static PluginManager instance;
	return instance;
}

PluginManager::PluginManager()
: _impl(std::make_unique<Impl>()) {
	s_manager = this;
}

PluginManager::~PluginManager() {
	deinitialize();
	s_manager = nullptr;
}

QString PluginManager::pluginsDirectory() const {
	return cWorkingDir() + u"tdata/plugins/"_q;
}

void PluginManager::initialize() {
	if (_impl->L) {
		return;
	}
	_impl->L = luaL_newstate();
	if (!_impl->L) {
		return;
	}
	luaL_openlibs(_impl->L);
	_impl->registerLuaBindings();

	QDir().mkpath(pluginsDirectory());
	reloadPlugins();
}

void PluginManager::deinitialize() {
	if (_impl->L) {
		lua_close(_impl->L);
		_impl->L = nullptr;
	}
	_impl->plugins.clear();
	_impl->profileStyles.clear();
}

void PluginManager::reloadPlugins() {
	_impl->plugins.clear();
	_impl->profileStyles.clear();

	const auto dir = QDir(pluginsDirectory());
	const auto list = dir.entryInfoList({ u"*.lua"_q, u"*.plugin"_q }, QDir::Files);
	for (const auto &fileInfo : list) {
		loadPlugin(fileInfo.absoluteFilePath());
	}
}

bool PluginManager::loadPlugin(const QString &filePath) {
	if (!_impl->L) {
		return false;
	}
	auto file = QFile(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto content = file.readAll();
	file.close();

	const auto meta = Ui::ParsePluginMetadata(content);
	auto info = PluginInfo{
		.id = meta.id.isEmpty() ? QFileInfo(filePath).baseName() : meta.id,
		.name = meta.name.isEmpty() ? QFileInfo(filePath).baseName() : meta.name,
		.version = meta.version.isEmpty() ? u"1.0"_q : meta.version,
		.author = meta.author,
		.description = meta.description,
		.filePath = filePath,
		.enabled = true,
	};

	const auto status = luaL_dostring(_impl->L, content.constData());
	if (status != LUA_OK) {
		const auto err = QString::fromUtf8(lua_tostring(_impl->L, -1));
		lua_pop(_impl->L, 1);
		LOG(("AyuPlugins: Failed to load %1: %2").arg(filePath, err));
		return false;
	}

	lua_getglobal(_impl->L, "on_load");
	if (lua_isfunction(_impl->L, -1)) {
		if (lua_pcall(_impl->L, 0, 0, 0) != LUA_OK) {
			const auto err = QString::fromUtf8(lua_tostring(_impl->L, -1));
			lua_pop(_impl->L, 1);
			LOG(("AyuPlugins: on_load error in %1: %2").arg(filePath, err));
		}
	} else {
		lua_pop(_impl->L, 1);
	}

	_impl->plugins.push_back(std::move(info));
	return true;
}

bool PluginManager::unloadPlugin(const QString &id) {
	const auto it = std::find_if(_impl->plugins.begin(), _impl->plugins.end(), [&](const PluginInfo &p) {
		return p.id == id;
	});
	if (it == _impl->plugins.end()) {
		return false;
	}
	QFile::remove(it->filePath);
	_impl->plugins.erase(it);
	reloadPlugins();
	return true;
}

const std::vector<PluginInfo> &PluginManager::plugins() const {
	return _impl->plugins;
}

bool PluginManager::isInstalled(const QString &id) const {
	return std::any_of(_impl->plugins.begin(), _impl->plugins.end(), [&](const PluginInfo &p) {
		return p.id == id;
	});
}

std::optional<CustomProfileStyle> PluginManager::getProfileStyle(uint64 peerId) const {
	const auto it = _impl->profileStyles.find(peerId);
	if (it != _impl->profileStyles.end()) {
		return it->second;
	}
	return std::nullopt;
}

void PluginManager::setProfileStyle(uint64 peerId, const CustomProfileStyle &style) {
	_impl->profileStyles[peerId] = style;
}

} // namespace AyuPlugins
