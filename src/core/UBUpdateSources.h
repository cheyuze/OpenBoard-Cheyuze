#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QUrl>

// Verification data must never come from an unaffiliated download proxy.
namespace UBUpdateSources
{
    inline QUrl websiteManifest()
    {
        return QUrl(QStringLiteral("https://xiwang.cheyuze.top/openboard/update.json"));
    }

    inline bool secureUrl(const QUrl &url)
    {
        return url.isValid() && !url.isEmpty()
                && url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
                && url.userInfo().isEmpty() && !url.hasFragment()
                && (url.port(-1) == -1 || url.port(-1) == 443);
    }

    inline bool trustedManifest(const QUrl &url)
    {
        if (!secureUrl(url))
            return false;
        const QString host = url.host().toLower();
        if (host == QStringLiteral("xiwang.cheyuze.top"))
            return url.path() == QStringLiteral("/openboard/update.json") && !url.hasQuery();
        if (host == QStringLiteral("github.com"))
            return url.path().startsWith(QStringLiteral("/cheyuze/OpenBoard-Cheyuze/releases/"));
        if (host == QStringLiteral("raw.githubusercontent.com"))
            return url.path().startsWith(QStringLiteral("/cheyuze/OpenBoard-Cheyuze/"))
                    && url.path().endsWith(QStringLiteral("/update.json"));
        // GitHub's signed release-asset redirects carry a query string.
        return host == QStringLiteral("release-assets.githubusercontent.com")
                || host == QStringLiteral("objects.githubusercontent.com");
    }

    inline QList<QUrl> manifests(const QUrl &configured = QUrl())
    {
        QList<QUrl> result { websiteManifest() };
        const QList<QUrl> candidates {
            configured,
            QUrl(QStringLiteral("https://github.com/cheyuze/OpenBoard-Cheyuze/releases/latest/download/update.json")),
            QUrl(QStringLiteral("https://raw.githubusercontent.com/cheyuze/OpenBoard-Cheyuze/main/update.json"))
        };
        for (const QUrl &candidate : candidates)
            if (trustedManifest(candidate) && !result.contains(candidate))
                result.append(candidate);
        return result;
    }

    inline bool websiteInstaller(const QUrl &url)
    {
        return secureUrl(url) && url.host() == QStringLiteral("xiwang.cheyuze.top")
                && QRegularExpression(QStringLiteral(
                    "^/openboard/releases/[0-9]+\\.[0-9]+\\.[0-9]+/OpenBoard-cheyuze-[0-9]+\\.[0-9]+\\.[0-9]+-x64\\.exe$"))
                   .match(url.path()).hasMatch() && !url.hasQuery();
    }

    inline bool githubInstaller(const QUrl &url)
    {
        return secureUrl(url) && url.host() == QStringLiteral("github.com")
                && QRegularExpression(QStringLiteral(
                    "^/cheyuze/OpenBoard-Cheyuze/releases/download/v[0-9]+\\.[0-9]+\\.[0-9]+/OpenBoard-cheyuze-[0-9]+\\.[0-9]+\\.[0-9]+-x64\\.exe$"))
                   .match(url.path()).hasMatch() && !url.hasQuery();
    }

    inline bool allowedInstaller(const QUrl &url)
    {
        if (websiteInstaller(url) || githubInstaller(url))
            return true;
        if (!secureUrl(url) || url.hasQuery())
            return false;
        const QString host = url.host();
        return (host == QStringLiteral("ghproxy.net") || host == QStringLiteral("gh-proxy.com")
                || host == QStringLiteral("gh-proxy.org"))
                && githubInstaller(QUrl(url.path().mid(1)));
    }

    inline QList<QUrl> installers(const QJsonObject &manifest, bool preferWebsite = true)
    {
        QList<QUrl> all;
        auto append = [&all](const QUrl &url) {
            if (allowedInstaller(url) && !all.contains(url))
                all.append(url);
        };
        append(QUrl(manifest.value(QStringLiteral("websiteUrl")).toString()));
        append(QUrl(manifest.value(QStringLiteral("githubUrl")).toString()));
        for (const auto &entry : manifest.value(QStringLiteral("urls")).toArray())
            append(QUrl(entry.toString()));
        append(QUrl(manifest.value(QStringLiteral("url")).toString()));

        QList<QUrl> result;
        for (const QUrl &url : all)
            if (preferWebsite ? websiteInstaller(url) : githubInstaller(url))
                result.append(url);
        for (const QUrl &url : all)
            if (!result.contains(url))
                result.append(url);
        return result;
    }

    inline bool validManifest(const QByteArray &bytes)
    {
        if (bytes.size() > 256 * 1024)
            return false;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return false;
        const QJsonObject object = document.object();
        return QRegularExpression(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+$"))
                .match(object.value(QStringLiteral("version")).toString()).hasMatch()
                && QRegularExpression(QStringLiteral("^[a-fA-F0-9]{64}$"))
                   .match(object.value(QStringLiteral("sha256")).toString()).hasMatch()
                && !installers(object).isEmpty();
    }
}
