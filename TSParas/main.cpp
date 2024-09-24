#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "HashProvider.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    HashProvider hashProvider;
    engine.rootContext()->setContextProperty("HashProvider", &hashProvider);

    const QUrl url(u"qrc:/TSParas/main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
