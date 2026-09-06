#include <QGuiApplication>
#include <QScreen>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <cstdio>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QJsonArray screens;
    for (auto screen : app.screens()) {
        auto r = screen->geometry();
        screens.append(QJsonObject{{"name", screen->name()}, {"x", r.x()}, {"y", r.y()},
            {"width", r.width()}, {"height", r.height()}, {"scale", screen->devicePixelRatio()}});
    }
    std::puts(QJsonDocument(screens).toJson(QJsonDocument::Compact).constData());
}
