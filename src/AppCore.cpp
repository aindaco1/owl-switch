#include "AppCore.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
#include <QVariantMap>
#include <QDebug>
#include <QRegularExpression>
#include <QQmlContext>
#include <QJSValue>
#include <QGuiApplication>
#include <QScreen>
#include <QNetworkInterface>
#include <QHostAddress>
#include <algorithm>

AppCore::AppCore(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot)
{
    migrateLegacyModuleSettings();

    QDir modulesDir(appRoot + "/modules");
    const QStringList dirs = modulesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &folder : dirs) {
        QString manifestPath = modulesDir.absoluteFilePath(folder + "/manifest.json");
        QFile f(manifestPath);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning("[AppCore] Bad manifest.json in %s: %s",
                     qPrintable(folder), qPrintable(err.errorString()));
            continue;
        }
        QJsonObject manifest = doc.object();
        QString id       = manifest["id"].toString();
        QString entryQml = manifest["entry_point_qml"].toString();
        if (id.isEmpty() || entryQml.isEmpty()) {
            qWarning("[AppCore] Skipping %s: manifest missing 'id' or 'entry_point_qml'",
                     qPrintable(folder));
            continue;
        }
        ModuleEntry m;
        m.id       = id;
        m.name     = manifest["name"].toString();
        m.folder   = folder;
        m.entryQml = entryQml;
        m.iconRel  = manifest["icon"].toString();
        m.settings = manifest["settings"].toArray().toVariantList();
        m.displayOrder = manifest["display_order"].toInt(1000);
        m.hidden   = manifest["hidden"].toBool(false);
        m_modules.append(m);
        qDebug("[AppCore] Loaded manifest: %s", qPrintable(id));
    }

    std::sort(m_modules.begin(), m_modules.end(), [](const ModuleEntry &a, const ModuleEntry &b) {
        if (a.displayOrder != b.displayOrder)
            return a.displayOrder < b.displayOrder;
        return a.name.localeAwareCompare(b.name) < 0;
    });
}

void AppCore::migrateLegacyModuleSettings()
{
    static const QMap<QString, QString> moduleIds = {
        {QStringLiteral("com.240mp.jellyfin"), QStringLiteral("com.owlswitch.jellyfin")},
        {QStringLiteral("com.240mp.karaoke"), QStringLiteral("com.owlswitch.karaoke")},
        {QStringLiteral("com.240mp.local_files"), QStringLiteral("com.owlswitch.local_files")},
        {QStringLiteral("com.240mp.nature"), QStringLiteral("com.owlswitch.nature")},
        {QStringLiteral("com.240mp.plex"), QStringLiteral("com.owlswitch.plex")},
        {QStringLiteral("com.240mp.retro_tv"), QStringLiteral("com.owlswitch.retro_tv")},
        {QStringLiteral("com.240mp.tumblr_screensaver"),
         QStringLiteral("com.owlswitch.tumblr_screensaver")}
    };

    QFile configFile(m_dataRoot + QStringLiteral("/config.json"));
    if (!configFile.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(configFile.readAll(), &error);
    configFile.close();
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;

    QJsonObject config = document.object();
    QJsonObject modules = config.value(QStringLiteral("modules")).toObject();
    bool changed = false;
    for (auto it = moduleIds.cbegin(); it != moduleIds.cend(); ++it) {
        if (!modules.contains(it.key()))
            continue;
        if (!modules.contains(it.value()))
            modules.insert(it.value(), modules.value(it.key()));
        modules.remove(it.key());
        changed = true;
    }

    QJsonObject app = config.value(QStringLiteral("app")).toObject();
    const QString legacyStartup = app.value(QStringLiteral("startup_module")).toString();
    const auto startupIt = moduleIds.constFind(legacyStartup);
    if (startupIt != moduleIds.cend()) {
        app.insert(QStringLiteral("startup_module"), startupIt.value());
        changed = true;
    }

    if (changed) {
        config.insert(QStringLiteral("app"), app);
        config.insert(QStringLiteral("modules"), modules);
        saveConfig(config);
        qInfo("[AppCore] Migrated legacy module settings to OwlSwitch identifiers");
    }
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------

QJsonObject AppCore::loadConfig() const {
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
    }
    // Return a sensible default if the file is missing or corrupt
    return QJsonObject{
        {"app", QJsonObject{{"color_scheme","Video 1"}}},
        {"modules", QJsonObject{}}
    };
}

void AppCore::saveConfig(const QJsonObject &config) const {
    QFile f(m_dataRoot + "/config.json");
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[AppCore] Could not write config.json: %s", qPrintable(f.errorString()));
        return;
    }
    f.write(QJsonDocument(config).toJson(QJsonDocument::Indented));
}

bool AppCore::isModuleEnabled(const ModuleEntry &module,
                              const QJsonObject &modulesConfig) const {
    const QJsonObject configured = modulesConfig.value(module.id).toObject();
    bool defaultEnabled = true;
    for (const QVariant &value : module.settings) {
        const QVariantMap setting = value.toMap();
        if (setting.value("key").toString() == QLatin1String("enabled")) {
            defaultEnabled = setting.value("default").toString().compare(
                                 "OFF", Qt::CaseInsensitive) != 0;
            break;
        }
    }
    return configured.contains("enabled") ? configured.value("enabled").toBool(true)
                                           : defaultEnabled;
}

// ---------------------------------------------------------------------------
// Q_INVOKABLE slots
// ---------------------------------------------------------------------------

void AppCore::scan_for_modules() {
    QJsonObject config = loadConfig();
    QJsonObject modulesConfig = config["modules"].toObject();

    QVariantList displayData;
    for (const auto &m : m_modules) {
        if (m.hidden) {
            qDebug("[AppCore] Module hidden: %s", qPrintable(m.name));
            continue;
        }

        if (!isModuleEnabled(m, modulesConfig)) {
            qDebug("[AppCore] Module disabled: %s", qPrintable(m.name));
            continue;
        }
        // entry_point is a path relative to APP_ROOT
        QString entryPoint = QStringLiteral("modules/%1/%2").arg(m.folder, m.entryQml);
        QVariantMap entry;
        entry["name"]        = m.name;
        entry["entry_point"] = entryPoint;
        displayData.append(entry);
        qDebug("[AppCore] Module: %s -> %s", qPrintable(m.name), qPrintable(entryPoint));
    }
    emit modulesLoaded(displayData);
}

QVariant AppCore::get_settings() {
    return loadConfig().toVariantMap();
}

QVariant AppCore::get_setting(const QString &moduleId, const QString &key) {
    QJsonObject config = loadConfig();
    QJsonObject target;
    if (moduleId.isEmpty())
        target = config["app"].toObject();
    else
        target = config["modules"].toObject()[moduleId].toObject();
    const QStringList parts = key.split('.', Qt::KeepEmptyParts);
    if (parts.size() == 2)
        return target.value(parts[0]).toObject().value(parts[1]).toVariant();
    return target.value(key).toVariant();
}

QVariantList AppCore::displayOptions() const {
    QVariantList options;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (int index = 0; index < screens.size(); ++index) {
        const QScreen *screen = screens.at(index);
        const QSize size = screen->geometry().size();
        options.append(QVariantMap{
            {QStringLiteral("id"), index},
            {QStringLiteral("label"), QStringLiteral("%1: %2 (%3x%4)")
                 .arg(index)
                 .arg(screen->name().isEmpty() ? QStringLiteral("Display") : screen->name())
                 .arg(size.width())
                 .arg(size.height())}
        });
    }
    return options;
}

QString AppCore::localIpAddress() const {
    static const QRegularExpression virtualInterface(
        QStringLiteral("^(docker|br-|bridge|veth|virbr|vmnet|vboxnet|utun|tun|tap|ipsec|zt|awdl|llw|anpi|ap\\d)"),
        QRegularExpression::CaseInsensitiveOption);

    QString best;
    int bestScore = -1;
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        const QNetworkInterface::InterfaceFlags flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack) ||
            interface.type() == QNetworkInterface::Virtual ||
            virtualInterface.match(interface.name()).hasMatch()) {
            continue;
        }

        int score = 0;
        if (interface.type() == QNetworkInterface::Ethernet)
            score = 2;
        else if (interface.type() == QNetworkInterface::Wifi)
            score = 1;
        if (score <= bestScore)
            continue;

        for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol ||
                address.isLoopback() || address.isLinkLocal()) {
                continue;
            }
            best = address.toString();
            bestScore = score;
            break;
        }
    }
    return best;
}

void AppCore::save_setting(const QString &moduleId, const QString &key, const QVariant &value) {
    QJsonObject config = loadConfig();
    const QVariant serializableValue = value.metaType() == QMetaType::fromType<QJSValue>()
        ? value.value<QJSValue>().toVariant() : value;

    // Navigate to the target section
    auto getTarget = [&]() -> QJsonObject {
        if (moduleId.isEmpty())
            return config["app"].toObject();
        return config["modules"].toObject()[moduleId].toObject();
    };
    auto setTarget = [&](const QJsonObject &target) {
        if (moduleId.isEmpty()) {
            config["app"] = target;
        } else {
            QJsonObject modules = config["modules"].toObject();
            modules[moduleId] = target;
            config["modules"] = modules;
        }
    };

    QJsonObject target = getTarget();

    // Handle dot-notation: "libraries.somekey" -> target["libraries"]["somekey"]
    QStringList parts = key.split('.', Qt::KeepEmptyParts);
    if (parts.size() == 2) {
        QJsonObject sub = target[parts[0]].toObject();
        sub[parts[1]] = QJsonValue::fromVariant(serializableValue);
        target[parts[0]] = sub;
    } else {
        target[key] = QJsonValue::fromVariant(serializableValue);
    }

    setTarget(target);
    saveConfig(config);

    qDebug("[AppCore] Setting saved: %s.%s = %s",
           qPrintable(moduleId.isEmpty() ? "app" : moduleId),
           qPrintable(key), qPrintable(serializableValue.toString()));

    if (moduleId.isEmpty())
        emit appSettingChanged(key, serializableValue.toString());
    else
        emit moduleSettingChanged(moduleId, key, serializableValue);
}

QVariant AppCore::get_module_info(const QString &moduleId) {
    for (const auto &m : m_modules) {
        if (m.id == moduleId) {
            QString iconPath = QStringLiteral("%1/modules/%2/%3")
                                   .arg(m_appRoot, m.folder, m.iconRel);
            QString iconUrl = QUrl::fromLocalFile(iconPath).toString();
            return QVariantMap{{"name", m.name}, {"icon", iconUrl}};
        }
    }
    return QVariantMap{};
}

QVariant AppCore::get_module_settings_schema(const QString &moduleId) {
    for (const auto &m : m_modules) {
        if (m.id == moduleId)
            return m.settings;
    }
    return QVariantList{};
}

void AppCore::invoke_module_action(const QString &moduleId, const QString &slotName) {
    auto it = m_backends.find(moduleId);
    if (it == m_backends.end()) {
        qWarning("[AppCore] invoke_module_action: no backend for '%s'", qPrintable(moduleId));
        return;
    }
    bool ok = QMetaObject::invokeMethod(it.value(), slotName.toLatin1().constData(),
                                        Qt::QueuedConnection);
    if (!ok)
        qWarning("[AppCore] invoke_module_action: slot '%s' not found on backend '%s'",
                 qPrintable(slotName), qPrintable(moduleId));
}

void AppCore::registerModule(const QString &moduleId, const QString &contextProperty,
                             QObject *backend, QQmlContext *ctx) {
    m_backends[moduleId] = backend;
    if (ctx)
        ctx->setContextProperty(contextProperty, backend);
    if (!backend) return;

    const QMetaObject *bmo = backend->metaObject();
    const QMetaObject *amo = this->metaObject();

    // dynamicOptionsReady(key, options) -> onBackendDynamicOptions (re-emit with moduleId)
    int sig = bmo->indexOfSignal(
        QMetaObject::normalizedSignature("dynamicOptionsReady(QString,QVariant)"));
    if (sig >= 0) {
        int slot = amo->indexOfSlot(
            QMetaObject::normalizedSignature("onBackendDynamicOptions(QString,QVariant)"));
        QMetaObject::connect(backend, sig, this, slot);
    }

    // authStateChanged() -> onBackendAuthStateChanged (re-emit with moduleId)
    sig = bmo->indexOfSignal(QMetaObject::normalizedSignature("authStateChanged()"));
    if (sig >= 0) {
        int slot = amo->indexOfSlot(
            QMetaObject::normalizedSignature("onBackendAuthStateChanged()"));
        QMetaObject::connect(backend, sig, this, slot);
    }

    // moduleSettingChanged(moduleId, key, value) -> backend.onSettingChanged(...)
    int slot = bmo->indexOfSlot(
        QMetaObject::normalizedSignature("onSettingChanged(QString,QString,QVariant)"));
    if (slot >= 0) {
        int s = amo->indexOfSignal(
            QMetaObject::normalizedSignature("moduleSettingChanged(QString,QString,QVariant)"));
        QMetaObject::connect(this, s, backend, slot);
    }
}

QString AppCore::moduleIdForBackend(QObject *backend) const {
    for (auto it = m_backends.constBegin(); it != m_backends.constEnd(); ++it) {
        if (it.value() == backend) return it.key();
    }
    return QString{};
}

void AppCore::onBackendDynamicOptions(const QString &key, const QVariant &options) {
    QString moduleId = moduleIdForBackend(sender());
    if (!moduleId.isEmpty())
        emit dynamicOptionsReady(moduleId, key, options);
}

void AppCore::onBackendAuthStateChanged() {
    QString moduleId = moduleIdForBackend(sender());
    if (!moduleId.isEmpty())
        emit moduleAuthStateChanged(moduleId);
}

QString AppCore::get_module_auth_state(const QString &moduleId) {
    auto it = m_backends.find(moduleId);
    if (it == m_backends.end()) return QString{};
    if (it.value()->metaObject()->indexOfMethod(
            QMetaObject::normalizedSignature("get_auth_state()")) < 0) {
        return QString{};
    }
    QString result;
    bool ok = QMetaObject::invokeMethod(
        it.value(), "get_auth_state",
        Qt::DirectConnection,
        Q_RETURN_ARG(QString, result)
    );
    if (!ok) return QString{};
    return result;
}

QVariant AppCore::get_installed_modules() {
    const QJsonObject modulesConfig = loadConfig().value("modules").toObject();
    QVariantList result;
    for (const auto &m : m_modules) {
        if (m.hidden)
            continue;
        result.append(QVariantMap{
            {"id",           m.id},
            {"name",         m.name},
            {"has_settings", !m.settings.isEmpty()},
            {"enabled",      isModuleEnabled(m, modulesConfig)},
            {"entry_point",  QStringLiteral("modules/%1/%2").arg(m.folder, m.entryQml)}
        });
    }
    return result;
}

QVariantMap AppCore::importColorScheme(const QJsonObject &obj) const {
    static const QStringList kRequiredKeys = {"primary","secondary","tertiary","surface","accent"};
    static const QRegularExpression kHexColor("^#[0-9A-Fa-f]{6}$");
    QVariantMap result;
    for (const QString &key : kRequiredKeys) {
        if (!obj.contains(key) || !obj[key].isString()) {
            qWarning("[AppCore] custom_color_scheme.json: missing or non-string key '%s'", qPrintable(key));
            return {};
        }
        QString value = obj[key].toString();
        if (!kHexColor.match(value).hasMatch()) {
            qWarning("[AppCore] custom_color_scheme.json: invalid hex color for '%s': %s",
                     qPrintable(key), qPrintable(value));
            return {};
        }
        result[key] = value;
    }
    return result;
}

QVariantMap AppCore::getCustomColorScheme() const {
    QFile file(m_dataRoot + "/custom_color_scheme.json");
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? importColorScheme(document.object()) : QVariantMap{};
}

QVariantMap AppCore::getCustomColorSchemes() const {
    static const QRegularExpression validName(QStringLiteral("^[^\\\"`]{3,28}$"));
    QFile file(m_dataRoot + "/custom_color_schemes.json");
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return {};
    QVariantMap schemes;
    const QJsonObject object = document.object();
    for (const QString &name : object.keys()) {
        if (!validName.match(name).hasMatch() || !object.value(name).isObject())
            continue;
        const QVariantMap scheme = importColorScheme(object.value(name).toObject());
        if (scheme.size() == 5) schemes.insert(name, scheme);
    }
    return schemes;
}

QVariantList AppCore::listDirectories(const QString &path) {
    QVariantList result;
    QDir dir(path);
    if (!dir.exists()) return result;
    const QStringList names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const QString &name : names) {
        QVariantMap item;
        item["name"] = name;
        item["path"] = dir.absoluteFilePath(name);
        result.append(item);
    }
    return result;
}

QString AppCore::parentDirectory(const QString &path) {
    QDir dir(path);
    if (!dir.cdUp()) return path;
    return dir.absolutePath();
}

QString AppCore::homePath() {
    return QDir::homePath();
}

QString AppCore::startupModuleEntryPoint() const {
    const QJsonObject config = loadConfig();
    const QString moduleId = config.value("app").toObject().value("startup_module").toString();
    if (moduleId.isEmpty() || moduleId == QLatin1String("None")) return {};
    const QJsonObject modulesConfig = config.value("modules").toObject();
    for (const ModuleEntry &module : m_modules) {
        if (!module.hidden && module.id == moduleId && isModuleEnabled(module, modulesConfig))
            return QStringLiteral("modules/%1/%2").arg(module.folder, module.entryQml);
    }
    return {};
}
