// This is the source code of AyuGram Mod for Desktop.
#pragma once

#include <QString>
#include <QColor>
#include <vector>
#include <optional>
#include <memory>

namespace AyuPlugins {

struct CustomProfileStyle {
	bool hasGradient = false;
	QColor gradientColor1;
	QColor gradientColor2;
	bool hasCustomBadge = false;
	QString badgeText;
	bool hasAvatarShape = false;
	int avatarRadius = 0;
};

struct PluginInfo {
	QString id;
	QString name;
	QString version;
	QString author;
	QString description;
	QString filePath;
	bool enabled = true;
};

class PluginManager final {
public:
	static PluginManager &Instance();

	void initialize();
	void deinitialize();

	void reloadPlugins();
	bool loadPlugin(const QString &filePath);
	bool unloadPlugin(const QString &id);

	[[nodiscard]] const std::vector<PluginInfo> &plugins() const;
	[[nodiscard]] bool isInstalled(const QString &id) const;

	[[nodiscard]] std::optional<CustomProfileStyle> getProfileStyle(uint64 peerId) const;
	void setProfileStyle(uint64 peerId, const CustomProfileStyle &style);

	[[nodiscard]] QString pluginsDirectory() const;

private:
	PluginManager();
	~PluginManager();

	PluginManager(const PluginManager &) = delete;
	PluginManager &operator=(const PluginManager &) = delete;

	struct Impl;
	std::unique_ptr<Impl> _impl;
};

} // namespace AyuPlugins
