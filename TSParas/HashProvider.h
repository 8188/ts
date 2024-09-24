#ifndef HASHPROVIDER_H
#define HASHPROVIDER_H

#include <QObject>
#include <QCryptographicHash>

class HashProvider : public QObject {
    Q_OBJECT
public:
    explicit HashProvider(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE QString hashPassword(const QString &password) {
        return QString(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
    }
};

#endif // HASHPROVIDER_H
